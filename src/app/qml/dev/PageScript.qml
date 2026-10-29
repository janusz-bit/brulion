import QtQuick
import Brulion

// PageScript — dev tool: puts the block editor of the *real app* into a state for screenshots
// (`brulion --demo --editor-script <state>`; tests/app/shell/screenshots.sh "page-*" shots), as a
// user would get there: typing goes through the inline editor, dialogs open as from the menu.
// The page view loads it next to its editor:
//   Loader { active: DevTools.editorScript !== "" && !DevTools.editor
//            sourceComponent: PageScript { editor: editor; controller: page } }
// States (blocks are found by their text on the sample notebook's guide page, else the first):
//   editing   edit a block with bold text (raw Markdown + highlighting, caret at the end)
//   editing-toast  the same, then a toast ("Scalono…": on phones at the top while typing)
//   toolbar-menu   (touch UIs) the same, the toolbar's "Więcej" — the block menu above it —, then
//             the system hides the keyboard (KeyboardInsets.simulated 0): the menu follows the
//             toolbar down, every item shown
//   suggest   a new block, "Zobacz też [[Prze" typed: page suggestions
//   tags      a new block, "Kupić farbę #pro" typed: tag suggestions
//   slash     a new block, "Kupić bilety /przy" typed: slash commands
//   date      a new block, "Odebrać paczkę ⏰ jutro o 9" typed: date suggestions
//   reminder  the reminder dialog for a new block "Kupić bilety na pociąg"
//   source    source mode (Ctrl+E)
//   sidepanel "Przepisy" opened in the side panel next to this page (wide windows)
//   empty     an existing but empty page ("Pusta strona.md" is written empty and opened)
//   doc-paragraph / doc-list / doc-heading   the guide is a document (Obsidian-like): editing its
//             first paragraph (caret in the middle), a list item of "Dokument i konspekt", that
//             section's heading
//   doc-outline  the guide switched to an outline (the header's toggle), nothing else changed
//   edit:<text>[@<caret>]  any page (--route): edit the block whose text starts with <text>
//             (one word: the screenshot scripts split arguments at spaces), caret at the end or
//             at <caret>
//   select:<text>  any page: the block whose text starts with <text> selected (as after Esc)
//   select-range:<first>..<last>  any page: the blocks from the one starting with <first> to the
//             one starting with <last> selected — on touch UIs in the selection mode (its tools
//             docked at the bottom), as after a long press and a drag
//   mode-tooltip   the tooltip of the page header's note mode toggle
//   properties  the page (--route) with its properties panel expanded (as the device setting
//             AppSettings.propertiesExpanded shows it), nothing focused, the view at the top — the
//             `ui` benchmark's "properties" screen
Item {
    id: script

    property BlockEditor editor
    property PageController controller
    property string scenario: DevTools.editorScript

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

    function rowOf(prefix) {
        const blocks = controller.blocks
        for (let i = 0; i < blocks.count; ++i) {
            if (blocks.get(i).plainText.indexOf(prefix) === 0)
                return i
        }
        return blocks.count > 0 ? 0 : -1
    }
    /// A new block after the block starting with `prefix`, with `text` typed into it.
    function typeAfter(prefix, text) {
        const row = controller.insertBlockAfter(rowOf(prefix), "")
        editor.startEditing(row, -1)
        typing.pending = text
        typing.start()
        return row
    }

    /// Runs `fn` a moment later (after the editor has its toolbar, the menu its place).
    Timer {
        id: later
        property var pending: null
        interval: 250
        function run(fn) {
            pending = fn
            restart()
        }
        onTriggered: if (pending) pending()
    }

    Timer {
        id: typing
        property string pending
        interval: 150
        onTriggered: {
            const edit = script.editor.activeEditor
            if (edit)
                edit.insert(edit.cursorPosition, pending)
        }
    }

    Timer {
        interval: 300
        running: script.editor !== null && script.controller !== null && script.controller.loaded
        onTriggered: {
            switch (script.scenario) {
            case "editing":
                script.editor.startEditing(script.rowOf("Każdy punkt listy"), -1)
                break
            case "editing-toast":
                script.editor.startEditing(script.rowOf("Każdy punkt listy"), -1)
                later.run(() => AppController.toast(qsTranslate("PageView", "Merged changes made elsewhere into this page"),
                                                    "info"))
                break
            case "toolbar-menu": {
                const row = script.rowOf("Każdy punkt listy")
                script.editor.startEditing(row, -1)
                later.run(() => {
                    const more = script.findItem(script.editor.dockHost, "toolbarMore")
                    const flick = script.findItem(script.editor.dockHost, "editToolbarFlick")
                    if (flick)
                        flick.contentX = Math.max(0, flick.contentWidth - flick.width)
                    if (more)
                        script.editor.openBlockMenuAbove(row, more)
                    later.run(() => KeyboardInsets.simulated = 0)
                })
                break
            }
            case "suggest":
                script.typeAfter("Link do strony", "Zobacz też [[Prze")
                break
            case "tags":
                script.typeAfter("Tagi zaczynają się", "Kupić farbę #pro")
                break
            case "slash":
                script.typeAfter("Zadzwonić do mamy", "Kupić bilety /przy")
                break
            case "date":
                script.typeAfter("Zadzwonić do mamy", "Odebrać paczkę ⏰ jutro o 9")
                break
            case "reminder": {
                const row = script.controller.insertBlockAfter(script.rowOf("Zadzwonić do mamy"), "Kupić bilety na pociąg")
                script.controller.stopEditing()
                script.editor.ensureRowVisible(row)
                script.editor.dialogs.createReminder(row, false)
                break
            }
            case "source":
                script.editor.toggleSourceMode()
                break
            case "sidepanel":
                AppController.openPage("Przepisy.md", -1, true)
                break
            case "doc-paragraph":
                script.editor.startEditing(script.rowOf("Brulion to notatnik"), 58)
                break
            case "doc-list":
                script.editor.startEditing(script.rowOf("Dokument, jak w Obsidianie"), -1)
                break
            case "doc-heading": {
                const row = script.rowOf("Dokument i konspekt")
                script.editor.startEditing(row, -1)
                break
            }
            case "doc-outline":
                script.controller.setMode("outline")
                break
            case "empty":
                if (script.controller.path !== "Pusta strona.md") {
                    Workspace.writeFile("Pusta strona.md", "")
                    AppController.openPage("Pusta strona.md", -1, false)
                }
                break
            case "properties":
                script.editor.setPropertiesExpanded(true)
                script.editor.listView.positionViewAtBeginning()
                break
            case "mode-tooltip": {
                // Shown as by a long press: it stays up without a hovering mouse.
                const toggle = script.findItem(script.editor, "noteModeToggle")
                if (toggle)
                    toggle._touchTip = true
                break
            }
            default:
                if (script.scenario.indexOf("select-range:") === 0) {
                    const ends = script.scenario.substring(13).split("..")
                    const first = script.rowOf(ends[0])
                    script.controller.selectRange(first, script.rowOf(ends.length > 1 ? ends[1] : ends[0]))
                    if (script.editor.touch)
                        script.editor.selectionMode = true
                    script.editor.listView.positionViewAtIndex(first, ListView.Center)
                } else if (script.scenario.indexOf("select:") === 0) {
                    const row = script.rowOf(script.scenario.substring(7))
                    script.editor.revealRow(row)
                } else if (script.scenario.indexOf("edit:") === 0) {
                    const spec = script.scenario.substring(5).split("@")
                    const row = script.rowOf(spec[0])
                    script.editor.startEditing(row, spec.length > 1 ? parseInt(spec[1]) : -1)
                }
                break
            }
        }
    }
}
