pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// PageScript — dev tool: puts the block editor of the *real app* into a state for screenshots
// (`brulion --demo --editor-script <state>`; tests/app/shell/screenshots.sh "page-*" shots), as a
// user would get there: typing goes through the inline editor, dialogs open as from the menu.
// The page view loads it next to its editor:
//   Loader { active: DevTools.editorScript !== "" && !DevTools.editor
//            sourceComponent: PageScript { editor: editor; controller: page } }
// States (blocks are found by their text on the sample notebook's guide page, else the first; the
// texts of the English guide, or of the Polish one when the UI is Polish — the sample notebook
// follows the UI language — written below as English / Polish):
//   editing   edit a block with bold text (raw Markdown + highlighting, caret at the end)
//   editing-toast  the same, then a toast ("Merged changes…": on phones at the top while typing)
//   toolbar-menu   (touch UIs) the same, the toolbar's "More" — the block menu above it —, then
//             the system hides the keyboard (KeyboardInsets.simulated 0): the menu follows the
//             toolbar down, every item shown
//   suggest   a new block, "See also [[Rec" / "Zobacz też [[Prze" typed: page suggestions
//   tags      a new block, "Buy paint #pro" / "Kupić farbę #pro" typed: tag suggestions
//   slash     a new block, "Buy tickets /remi" / "Kupić bilety /przy" typed: slash commands
//   date      a new block, "Pick up the parcel ⏰ tomorrow at 9" / "Odebrać paczkę ⏰ jutro o 9"
//             typed: date suggestions
//   reminder  the reminder dialog for a new block "Buy train tickets" / "Kupić bilety na pociąg"
//   source    source mode (Ctrl+E)
//   sidepanel "Recipes" / "Przepisy" opened in the side panel next to this page (wide windows)
//   empty     an existing but empty page ("Empty page.md" / "Pusta strona.md" is written empty and
//             opened)
//   doc-paragraph / doc-list / doc-heading   the guide is a document (Obsidian-like): editing its
//             first paragraph (caret in the middle), a list item of "Documents and outlines" /
//             "Dokument i konspekt", that section's heading
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

    /// The item named `name` under `item` (depth first), or null (a typed function: its result
    /// stays valid across the calls that follow it).
    function findItem(item: Item, name: string): Item { return DevTest.findItem(item, name) }

    /// `en`, or `pl` when the UI (and so the sample notebook) is Polish.
    function say(en: string, pl: string): string {
        return DevTools.language === "pl" ? pl : en
    }
    readonly property string emptyPage: script.say("Empty page.md", "Pusta strona.md")
    /// Blocks of the sample notebook's guide that the scenarios work on.
    readonly property string listItem: script.say("Every list item", "Każdy punkt listy")
    readonly property string callMom: script.say("Call Mom", "Zadzwonić do mamy")

    /// The fields of a block (BlockListModel.get()).
    property DevBlock block: DevBlock {}
    function rowOf(prefix: string): int {
        const blocks = script.controller.blocks
        for (let i = 0; i < blocks.count; ++i) {
            script.block.read(blocks.get(i))
            if (Js.indexOf(script.block.plainText, prefix) === 0)
                return i
        }
        return blocks.count > 0 ? 0 : -1
    }
    /// A new block after the block starting with `prefix`, with `text` typed into it.
    function typeAfter(prefix: string, text: string): int {
        const row = script.controller.insertBlockAfter(script.rowOf(prefix), "")
        script.editor.startEditing(row, -1)
        typing.pending = text
        typing.start()
        return row
    }
    /// The reminder dialog for the new block `row`.
    function reminderFor(row: int): void {
        script.controller.stopEditing()
        script.editor.ensureRowVisible(row)
        script.dialogs().createReminder(row, false)
    }
    function dialogs(): EditorDialogs { return script.editor.dialogs as EditorDialogs }
    function list(): ListView { return script.editor.listView }
    function edit(): InlineEditor { return script.editor.activeEditor }
    /// The block the scenario works on (the second half of "toolbar-menu").
    property int scenarioRow: -1
    /// The second half of a scenario, a moment later (after the editor has its toolbar, the menu
    /// its place): `step` 1, then 2.
    function runLater(step: int): void {
        switch (script.scenario) {
        case "editing-toast":
            AppController.toast(qsTranslate("PageView", "Merged changes made elsewhere into this page"), "info")
            break
        case "toolbar-menu":
            if (step === 1) {
                const more = script.findItem(script.editor.dockHost, "toolbarMore")
                const flick = script.findItem(script.editor.dockHost, "editToolbarFlick") as Flickable
                if (flick)
                    flick.contentX = Math.max(0, flick.contentWidth - flick.width)
                if (more)
                    script.editor.openBlockMenuAbove(script.scenarioRow, more)
                later.run(2)
            } else {
                KeyboardInsets.simulated = 0
            }
            break
        }
    }

    /// Runs runLater() a moment later (after the editor has its toolbar, the menu its place).
    Timer {
        id: later
        property int step: 0
        interval: 250
        function run(next: int): void {
            step = next
            restart()
        }
        onTriggered: script.runLater(step)
    }

    Timer {
        id: typing
        property string pending
        interval: 150
        onTriggered: {
            const edit = script.edit()
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
                script.editor.startEditing(script.rowOf(script.listItem), -1)
                break
            case "editing-toast":
                script.editor.startEditing(script.rowOf(script.listItem), -1)
                later.run(1)
                break
            case "toolbar-menu":
                script.scenarioRow = script.rowOf(script.listItem)
                script.editor.startEditing(script.scenarioRow, -1)
                later.run(1)
                break
            case "suggest":
                script.typeAfter(script.say("A link to a page", "Link do strony"),
                                 script.say("See also [[Rec", "Zobacz też [[Prze"))
                break
            case "tags":
                script.typeAfter(script.say("Tags start with", "Tagi zaczynają się"),
                                 script.say("Buy paint #pro", "Kupić farbę #pro"))
                break
            case "slash":
                script.typeAfter(script.callMom,
                                 script.say("Buy tickets /remi", "Kupić bilety /przy"))
                break
            case "date":
                script.typeAfter(script.callMom,
                                 script.say("Pick up the parcel ⏰ tomorrow at 9", "Odebrać paczkę ⏰ jutro o 9"))
                break
            case "reminder":
                script.reminderFor(script.controller.insertBlockAfter(script.rowOf(script.callMom),
                                                                      script.say("Buy train tickets", "Kupić bilety na pociąg")))
                break
            case "source":
                script.editor.toggleSourceMode()
                break
            case "sidepanel":
                AppController.openPage(script.say("Recipes.md", "Przepisy.md"), -1, true)
                break
            case "context": // the side panel's accordion of this page (wide windows)
                AppController.clearSidePanelRoute()
                AppController.sidePanelVisible = true
                break
            case "doc-paragraph":
                script.editor.startEditing(script.rowOf(script.say("Brulion is a notebook", "Brulion to notatnik")), 58)
                break
            case "doc-list":
                script.editor.startEditing(script.rowOf(script.say("A document, as in Obsidian", "Dokument, jak w Obsidianie")), -1)
                break
            case "doc-heading":
                script.editor.startEditing(script.rowOf(script.say("Documents and outlines", "Dokument i konspekt")), -1)
                break
            case "doc-outline":
                script.controller.setMode("outline")
                break
            case "empty":
                if (script.controller.path !== script.emptyPage) {
                    Workspace.writeFile(script.emptyPage, "")
                    AppController.openPage(script.emptyPage, -1, false)
                }
                break
            case "properties":
                script.editor.setPropertiesExpanded(true)
                script.list().positionViewAtBeginning()
                break
            case "mode-tooltip": {
                // Shown as by a long press: it stays up without a hovering mouse.
                const toggle = script.findItem(script.editor, "noteModeToggle")
                if (toggle)
                    DevTest.setProperty(toggle, "_touchTip", true)
                break
            }
            default:
                script.byPrefix()
                break
            }
        }
    }
    /// The scenarios with an argument: select-range:, select:, edit:.
    function byPrefix(): void {
        if (Js.indexOf(script.scenario, "select-range:") === 0) {
            const first = script.rowOf(script.ends()[0])
            const last = script.rowOf(script.rangeEnd())
            script.controller.selectRange(first, last)
            if (script.editor.touch)
                script.editor.selectionMode = true
            script.list().positionViewAtIndex(first, ListView.Center)
        } else if (Js.indexOf(script.scenario, "select:") === 0) {
            const row = script.rowOf(Js.substringFrom(script.scenario, 7))
            script.editor.revealRow(row)
        } else if (Js.indexOf(script.scenario, "edit:") === 0) {
            script.editor.startEditing(script.rowOf(script.editSpec()[0]), script.editCaret())
        }
    }
    /// "select-range:<first>..<last>" → [first, last]; "edit:<text>[@<caret>]" → [text, caret].
    function ends(): list<string> {
        return Js.split(Js.substringFrom(script.scenario, 13), "..")
    }
    function rangeEnd(): string {
        const parts = script.ends()
        return parts.length > 1 ? parts[1] : parts[0]
    }
    function editCaret(): int {
        const parts = script.editSpec()
        return parts.length > 1 ? Js.parseInt(parts[1]) : -1
    }
    function editSpec(): list<string> {
        return Js.split(Js.substringFrom(script.scenario, 5), "@")
    }
}
