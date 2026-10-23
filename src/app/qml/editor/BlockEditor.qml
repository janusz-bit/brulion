pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// BlockEditor — one page as a Logseq-like outline of blocks (docs/UI.md "The block editor"), or,
// in the document mode (controller.documentMode, docs/UI.md "Note modes"), as an Obsidian-like
// continuous document: no bullets, guides or fold arrows for paragraphs and headings, lists as
// ordinary Markdown lists in the flow, larger headings and paragraph spacing; the same inline
// editing. Document keys: Enter on an empty list item / Backspace at its start leave the list,
// Tab / Shift+Tab only in list items.
//
//   PageController { id: page; host: Workspace; path: "Projekty/Dom.md" }
//   BlockEditor { anchors.fill: parent; controller: page; header: Component { PageTitle {} } }
//
// A ListView over controller.blocks (BlockListModel): unfocused blocks show rendered rich text
// (core InlineRenderer, links page:/tag:/block:/ext:/task:/reminder:), the block being edited a
// TextEdit with the raw Markdown and MarkdownHighlighter. The editing row and caret live in the
// controller, so delegates may be destroyed and recreated by the ListView at any time.
//
// Keys (inline editor): Enter split · Shift+Enter line break · Backspace at start merge (a task
// first loses its checkbox) · Delete at end merge next · Tab / Shift+Tab indent / outdent ·
// Alt+Up/Down move · Up/Down at the first/last line go to the previous/next block · Ctrl+Enter
// cycle task · Ctrl+Shift+Enter the task's status menu · Ctrl+. fold · Ctrl+Shift+C copy the
// block reference ((id)) · Esc stop editing (select block) · Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y
// undo/redo · Ctrl+E source mode. Selected block (after Esc): Up/Down select, Enter edit, Delete
// remove, Tab/Shift+Tab, Alt+Up/Down, Ctrl+Enter, Ctrl+Shift+Enter, Ctrl+., Ctrl+Shift+C, Esc clear.
// The block menu (BlockMenu: zoom, fold, "Kopiuj odnośnik" — links to the block or heading —,
// picture, indent, move, delete) opens with a right-click anywhere on a block (mouse, touchpad,
// stylus), from a bullet or number (also a long press) and from the touch toolbar's "Więcej".
// copyLink() / copyCurrentLink() copy links and say so (notice); a link to a block or heading of
// the page scrolls there, selects it and flashes it (revealRow()); a missing one is said.
// Autocomplete (`[[`, `((`, `#`, `/`, dates after ⏰/📅/@) in SuggestionPopup; slash commands
// that need input open EditorDialogs; on touch UIs MobileEditToolbar is docked on the keyboard:
// at the bottom edge of `toolbarHost` (default: the editor), which the shell keeps on the
// keyboard's top (KeyboardInsets) — for a block, a table cell or the source (toolbarMode).
// Pictures: Ctrl+V pastes a copied picture (or copied files) as an attachment `![[…]]`, files
// dropped on the page are stored and embedded (in the block being edited at the caret, else in
// a new block after the one under the drop), "/Obraz…" and the toolbar pick files.
// A leading "# Title" repeating the page title is shown above the page properties
// (PageController.titleRow, see BlockDelegate); Up/Down follow that on-screen order.
// `notice(text, kind)` tells about problems the host should show as a toast.
// Input methods: a word still being composed (preedit — Android keyboards compose every word)
// counts for completions and is committed before Enter/Backspace/Tab/… are handled, before
// toolbar actions and when the app is left. Keys typed while the edited block's delegate is
// scrolled away bring it back and go to it.
FocusScope {
    id: editor

    property PageController controller
    /// Shown above the blocks (e.g. the page title); scrolls with them.
    property Component header: null
    /// Maximum width of the text column (centred when the editor is wider).
    property real maxTextWidth: Theme.measure
    property real horizontalPadding: Theme.compact ? Theme.space3 : Theme.space8
    /// Extra space below the last block (click there to add a block).
    property real bottomSpace: Theme.space16 * 2
    /// Document mode: the size of the largest heading (0: no limit). The journal stream sets the
    /// size of its days' dates, so a "## Praca" inside a day never outranks the day itself.
    property int headingCap: 0
    /// The editor is as tall as its blocks and something around it scrolls (a day of the journal
    /// stream): all blocks are created at once — with the editor itself when its controller is set
    /// from the start — so the editor has its final height as soon as it exists. (A list that
    /// created them as it grew made the day grow after the journal list had placed it.)
    property bool autoHeight: false

    // ---- Geometry shared by the delegates ---------------------------------------------------
    /// The page is shown as a document (Obsidian-like), not as an outline.
    readonly property bool documentMode: controller !== null && controller.documentMode
    readonly property int fontSize: Theme.textEditor
    readonly property int lineHeight: Theme.lineHeightPx(fontSize, Theme.lineHeightLoose)
    readonly property real indentStep: Theme.compact ? 20 : 26
    /// Bullet column (the bullet is centred in it) and the fold arrow column left of it.
    readonly property real bulletColumn: Theme.touchUi ? 22 : 20
    readonly property real arrowColumn: Theme.touchUi ? 18 : 16
    /// Room left of the text column: the outline's fold arrows and bullets; in a document
    /// `documentInset` (none on a page).
    readonly property real gutter: documentMode ? documentInset : outlineGutter
    readonly property real outlineGutter: arrowColumn + bulletColumn
    /// Document mode: room left of the text column. The journal stream gives a document day the
    /// outline's gutter when its other days are outlines, so all days share one left edge.
    property real documentInset: 0
    // Document mode: the marker column of a list level (bullet, number or checkbox) — the text
    // of a list item starts one column in per level — and the vertical rhythm of a page of text.
    readonly property real listIndent: Math.round(fontSize * (Theme.compact ? 1.5 : 1.7))
    readonly property int paragraphGap: Math.round(fontSize * 0.75)
    readonly property int listGap: Theme.touchUi ? 4 : 2
    readonly property int headingGapBelow: Math.round(fontSize * 0.3)
    function headingGapAbove(level) {
        return Math.round(fontSize * (level === 1 ? 1.6 : level === 2 ? 1.3 : level === 3 ? 1.05 : 0.85))
    }
    readonly property real taskBoxWidth: Theme.touchUi ? 26 : 24
    readonly property real contentX: Math.max(horizontalPadding, (width - maxTextWidth) / 2)
    readonly property real contentWidth: Math.max(120, Math.min(maxTextWidth, width - 2 * horizontalPadding))
    /// Where the caret/selection lives: the inline editor (TextEdit) being typed in.
    property InlineEditor activeEditor: null
    readonly property bool touch: Theme.touchUi

    // ---- The touch toolbar, docked on the keyboard (docs/UI.md "On-screen keyboard") ------------
    /// The table whose cell is being edited (TableBlockView sets and clears it), else null.
    property TableBlockView editingTable: null
    /// Source mode with its text being edited (SourceEditor: the text has the keyboard).
    property bool sourceEditing: false
    /// The tools of the touch toolbar: "block" (a block is being edited), "cell" (a table cell),
    /// "source" (source mode), "" (none: no toolbar). Stays while the system hides the keyboard
    /// and the editing goes on. One slot: another tool set docks the same way (a new mode).
    readonly property string toolbarMode: !touch || controller === null ? ""
                                          : controller.sourceMode ? (sourceEditing ? "source" : "")
                                          : controller.editingRow >= 0 ? "block"
                                          : editingTable !== null ? "cell" : ""
    readonly property bool toolbarVisible: toolbarMode !== ""
    readonly property real toolbarHeight: Theme.editToolbarHeight
    /// Where the toolbar docks: an item that does not scroll with the blocks and whose bottom
    /// edge the shell keeps on the keyboard — the editor itself (null) on a page, the journal
    /// stream's frame for its days (ContentJournalDay). The toolbar fills its bottom edge.
    property Item toolbarHost: null
    readonly property Item dockHost: toolbarHost !== null ? toolbarHost : editor
    /// What scrolls the editor instead of its own list (a day of the journal stream: the journal
    /// list; any object with ensureVisible(item, rect)), else null.
    property var scroller: null
    /// The source's text editor in source mode (the toolbar inserts into it), else null.
    readonly property TextEdit sourceTextEdit: sourceLoader.item ? (sourceLoader.item as SourceEditor).textEdit : null

    /// The bottom of what is seen of the editor, in its own coordinates: the toolbar's top while it
    /// shows, else the bottom of the dock host (on the keyboard). Computed when asked: the days of
    /// the journal stream move under their fixed host.
    function visibleBottom(): real {
        const host = dockHost
        const bottom = host.height - (toolbarVisible ? toolbarHeight : 0)
        return host === editor ? bottom : editor.mapFromItem(host, 0, bottom).y
    }
    /// Popup margin that keeps a menu above the toolbar and the keyboard (window coordinates).
    function menuBottomMargin(): real {
        const win = editor.Window.window
        if (!win)
            return Theme.space2
        return Math.max(Theme.space2, win.height - editor.mapToItem(null, 0, visibleBottom()).y + Theme.space2)
    }

    /// Something to tell the user (a toast): kind "error" | "info" | "success".
    signal notice(string text, string kind)

    // The title above the properties panel (BlockDelegate trades their places on screen): set by
    // those two delegates.
    readonly property bool titleFirst: controller !== null && controller.titleRow > 0 && controller.zoomBlock < 0
    property real titleHeight: 0
    property real propertiesHeight: 0

    /// The next / previous visible row in on-screen order (the title above the properties).
    function visualNext(row) {
        if (titleFirst) {
            const t = controller.titleRow
            if (row === -1)
                return t
            if (row === t)
                return t - 1
            if (row === t - 1)
                return controller.nextVisibleRow(t)
        }
        return controller.nextVisibleRow(row)
    }
    function visualPrevious(row) {
        if (titleFirst) {
            const t = controller.titleRow
            if (row === t)
                return -1
            if (row === t - 1)
                return t
            if (controller.previousVisibleRow(row) === t)
                return t - 1
        }
        return controller.previousVisibleRow(row)
    }

    // Navigation between blocks with Up/Down keeps the caret's x position.
    property real navigationX: -1
    property string navigationEdge: ""
    // A click on rendered text: where the caret should go once the inline editor exists.
    property int pendingClickRow: -1
    property point pendingClick: Qt.point(0, 0)

    // Fonts of the block kinds: body text and headings (outline: levels 1-3, deeper levels use
    // the body size in bold; document: larger, relative to the text size, levels 1-4). Fixed
    // pixel line heights, see Theme.lineHeightPx.
    function headingSize(level) {
        if (documentMode) {
            if (level < 1 || level > 4)
                return fontSize
            // headingCap compresses the steps above the body size (the levels stay apart).
            const top = fontSize * 1.75
            const k = headingCap > fontSize && headingCap < top ? (headingCap - fontSize) / (top - fontSize) : 1
            return Math.round(fontSize + fontSize * ([1.75, 1.45, 1.25, 1.1][level - 1] - 1) * k)
        }
        return level === 1 ? Theme.text2xl : level === 2 ? Theme.textXl : level === 3 ? Theme.textLg : fontSize
    }
    function headingLineHeight(level) {
        if (documentMode)
            return level >= 1 && level <= 4 ? Theme.lineHeightPx(headingSize(level), 1.3) : lineHeight
        return level >= 1 && level <= 3 ? Theme.lineHeightPx(headingSize(level), 1.4) : lineHeight
    }
    /// Baseline of the first line (from the top of a text item) for a heading level (0 = body);
    /// measured on hidden probes laid out exactly like the blocks, so bullets, checkboxes and
    /// fold arrows sit on the first line with any font.
    function firstBaseline(level) {
        switch (level) {
        case 1: return probeH1.firstBaselineY
        case 2: return probeH2.firstBaselineY
        case 3: return probeH3.firstBaselineY
        case 4: return probeH4.firstBaselineY
        default: return probeBody.firstBaselineY
        }
    }
    function xHeight(level) {
        switch (level) {
        case 1: return probeH1.xHeight
        case 2: return probeH2.xHeight
        case 3: return probeH3.xHeight
        case 4: return probeH4.xHeight
        default: return probeBody.xHeight
        }
    }
    /// Vertical centre of the x-height of the first line: where bullets and checkboxes go.
    function firstLineCenter(level) {
        return firstBaseline(level) - xHeight(level) / 2
    }

    component LineProbe: TextEdit {
        id: probe
        property int level: 0
        readonly property real firstBaselineY: { void probe.contentHeight; void probe.font; return probe.positionToRectangle(0).y + probeMetrics.ascent }
        readonly property real xHeight: probeMetrics.xHeight
        visible: false
        enabled: false
        text: "x"
        textFormat: TextEdit.PlainText
        font.family: Theme.fontEditor
        font.pixelSize: editor.headingSize(level)
        font.weight: level > 0 ? Theme.weightSemiBold : Theme.weightNormal
        FontMetrics { id: probeMetrics; font: probe.font }
        MarkdownHighlighter { textDocument: probe.textDocument; mode: "code"; lineHeight: editor.headingLineHeight(probe.level) }
    }
    LineProbe { id: probeBody; level: 0 }
    LineProbe { id: probeH1; level: 1 }
    LineProbe { id: probeH2; level: 2 }
    LineProbe { id: probeH3; level: 3 }
    LineProbe { id: probeH4; level: 4 }

    readonly property var rendererPalette: ({
        link: Theme.link, unresolved: Theme.linkMissing, muted: Theme.fgMuted,
        codeBackground: Theme.bgSunken, highlightBackground: Theme.mark,
        tagFg: Theme.tagFg, tagBg: Theme.tagBg,
        dueFg: Theme.chipDueFg, dueBg: Theme.chipDueBg,
        overdueFg: Theme.chipOverdueFg, overdueBg: Theme.chipOverdueBg,
        scheduledFg: Theme.chipScheduledFg, scheduledBg: Theme.chipScheduledBg,
        reminderFg: Theme.chipReminderFg, reminderBg: Theme.chipReminderBg,
        alarmFg: Theme.chipAlarmFg, alarmBg: Theme.chipAlarmBg,
        recurrenceFg: Theme.chipRecurrenceFg, recurrenceBg: Theme.chipRecurrenceBg,
        doneFg: Theme.chipDoneFg, doneBg: Theme.chipDoneBg,
        neutralFg: Theme.chipNeutralFg, neutralBg: Theme.chipNeutralBg
    })
    readonly property var highlighterPalette: ({
        heading: Theme.fg, marker: Theme.fgFaint, link: Theme.link, url: Theme.link, tag: Theme.tagFg,
        code: Theme.fg, codeBackground: Theme.bgSunken, highlightBackground: Theme.mark,
        muted: Theme.fgMuted, faint: Theme.fgFaint, due: Theme.chipDueFg, scheduled: Theme.chipScheduledFg,
        reminder: Theme.chipReminderFg, alarm: Theme.warningText, recurrence: Theme.chipRecurrenceFg,
        done: Theme.fgFaint, priority: Theme.dangerText
    })

    // Also set at once when the controller arrives, before the list creates its rows: a Binding
    // applies its value only when the editor has been completed — after the rows exist — and the
    // new palette (or day) made every row render its text a second time.
    onControllerChanged: {
        if (!controller)
            return
        controller.palette = rendererPalette
        controller.locale = DatesHelper.locale
        controller.now = DatesHelper.fixedNow
    }
    Binding { target: editor.controller; property: "palette"; value: editor.rendererPalette; when: editor.controller !== null }
    Binding { target: editor.controller; property: "locale"; value: DatesHelper.locale; when: editor.controller !== null }
    // The fixed clock of screenshots and tests. Midnight itself: PageController follows the
    // DayClock ("jutro 17:30" becomes "dziś 17:30", overdue dates turn red).
    Binding { target: editor.controller; property: "now"; value: DatesHelper.fixedNow; when: editor.controller !== null }

    Accessible.role: Accessible.Pane
    Accessible.name: controller ? controller.title : ""
    // Scrolled blocks must not draw over what surrounds the editor (the shell's top bar).
    clip: true

    /// False once the editor is being destroyed (see later()).
    property bool alive: true
    Component.onDestruction: alive = false

    /// Qt.callLater for the editor's deferred work: `fn` runs once the event loop is idle — unless
    /// the editor went away meanwhile (a day of the journal stream scrolled out of view, the page
    /// closed). Qt.callLater does not tie an arrow function to the component that made it: run
    /// after the editor is gone it met a null `list` (TypeError). Ids (editor, list) read null
    /// then, so they are what is checked; `fn` should use ids or qualified properties only.
    function later(fn) {
        Qt.callLater(() => {
            if (editor && list && editor.alive)
                fn()
        })
    }

    // ---- Editing helpers called by delegates and the inline editors ------------------------------

    /// Start editing `row` at `cursor` (-1 = end) and give it the keyboard.
    function startEditing(row, cursor) {
        if (!controller || row < 0)
            return
        closeSuggestions()
        const again = controller.editingRow === row
        controller.startEditing(row, cursor === undefined ? -1 : cursor)
        if (again && activeEditor && activeEditor.row === row)
            activeEditor.ensureFocus()
        ensureRowVisible(row)
    }

    /// Scroll to `row` (unfolding / leaving a zoom as needed), select it and flash it — for routes
    /// with `line` / `blockId` / `heading` (revealRow(controller.rowOfBlockId(route.blockId))), links
    /// to a block or heading of the page, the side panel's outline.
    function revealRow(row) {
        if (!controller || row < 0)
            return
        controller.reveal(row)
        controller.stopEditing()
        controller.selectedRow = row
        // The header (page title) settling its height must not scroll back to the top.
        list.userMoved = true
        later(() => {
            list.positionViewAtIndex(row, ListView.Center)
            // The row's delegate exists once the list has been laid out there.
            list.forceLayout()
            const item = list.itemAtIndex(row) as BlockDelegate
            if (item)
                item.flash()
        })
        list.forceActiveFocus()
    }
    /// A link's block (`blockId`) or heading is not on this page (nor anywhere): say so.
    function noticeMissing(blockId, heading) {
        if (blockId)
            notice(qsTr("Block ^%1 is not on this page").arg(blockId), "info")
        else if (heading)
            notice(qsTr("Heading “%1” is not on this page").arg(heading), "info")
    }

    // ---- Copying links (docs/SYNTAX.md §2 "Copied links") -----------------------------------------

    /// Copy a link to `row` — kind "blockRef" ((id)), "blockLink" [[Page#^id]], "heading"
    /// [[Page#Heading]], "embed", "page" [[Page]] (PageController.copyLink) — and say so: "Skopiowano
    /// …", or why nothing was copied. True when copied.
    function copyLink(row, kind) {
        if (!controller)
            return false
        // A word the keyboard is still composing becomes text first (an id resets the text).
        if (activeEditor && activeEditor.row === row)
            EditorInput.commitPreedit(activeEditor)
        const r = controller.copyLink(row, kind)
        if (r.ok)
            notice(qsTr("Copied %1").arg(r.text), "success")
        else
            notice(copyFailure(r.reason), r.reason === "noClipboard" || r.reason === "noId" ? "error" : "info")
        return r.ok
    }
    /// Why copyLink() copied nothing (PageController.copyLink's reason).
    function copyFailure(reason) {
        switch (reason) {
        case "readOnly":
            return qsTr("Links to blocks are not made in the documentation notebook: they would change its pages. Copy a link to a heading or to the page instead.")
        case "noId":
            return qsTr("This block cannot have a link of its own. Copy a link to the page instead.")
        case "notHeading":
            return qsTr("This block is not a heading")
        case "sourceMode":
            return qsTr("Switch back to blocks (Ctrl+E) to copy a link to a block")
        case "noClipboard":
            return qsTr("Could not copy: the clipboard is not available")
        case "noPage":
            return qsTr("Could not copy: the page has no file yet")
        default:
            return qsTr("First select a block or put the caret in it")
        }
    }
    /// The command palette and Ctrl+Shift+C: a link to the page ("page"), to the current block —
    /// the one being edited, else the selected one — ("blockRef", "blockLink") or to the heading of
    /// its section ("heading": the last heading at or above it, as the side panel's outline marks).
    function copyCurrentLink(kind) {
        if (!controller)
            return false
        if (kind === "page")
            return copyLink(-1, "page")
        if (controller.sourceMode) {
            notice(copyFailure("sourceMode"), "info")
            return false
        }
        const current = controller.editingRow >= 0 ? controller.editingRow : controller.selectedRow
        if (current < 0) {
            notice(copyFailure("noRow"), "info")
            return false
        }
        if (kind !== "heading")
            return copyLink(current, kind)
        const heading = controller.headingRowFor(current)
        if (heading < 0) {
            notice(qsTr("There is no heading above this block"), "info")
            return false
        }
        return copyLink(heading, "heading")
    }

    /// A click on the rendered text of `row` at (x, y) in that text's coordinates.
    function editAt(row, x, y) {
        pendingClickRow = row
        pendingClick = Qt.point(x, y)
        startEditing(row, -1)
    }

    function stopEditing(select) {
        if (!controller)
            return
        const row = controller.editingRow
        closeSuggestions()
        controller.stopEditing()
        if (select && row >= 0)
            controller.selectedRow = row
        list.forceActiveFocus()
    }

    function toggleSourceMode() {
        if (!controller)
            return
        closeSuggestions()
        controller.save()
        controller.sourceMode = !controller.sourceMode
    }

    function ensureRowVisible(row) {
        later(() => {
            if (!editor.controller || row < 0 || row >= list.count)
                return
            if (list.itemAtIndex(row) === null)
                list.positionViewAtIndex(row, ListView.Contain)
        })
    }

    /// Keep `rect` of `item` (e.g. the caret) inside the visible part of the list — or of the
    /// list scrolling the editor (`scroller`).
    function ensureVisible(item, rect) {
        if (!item || !list || !editor.alive)
            return
        if (editor.scroller) {
            editor.scroller.ensureVisible(item, rect)
            return
        }
        const p = item.mapToItem(list.contentItem, rect.x, rect.y)
        const margin = editor.lineHeight
        const bottomLimit = list.height - list.bottomMargin
        if (p.y - list.contentY < margin)
            list.contentY = Math.max(list.originY - list.topMargin, p.y - margin)
        else if (p.y + rect.height - list.contentY > bottomLimit - margin)
            list.contentY = Math.min(p.y + rect.height - bottomLimit + margin,
                                     Math.max(list.originY, list.contentHeight + list.originY - list.height + list.bottomMargin))
    }

    /// Keep the caret of an inline editor visible (called after layout settled). Not while the
    /// keyboard slides (the view changes size every frame): once it has settled.
    function ensureCaretVisible(edit) {
        if (edit && edit.activeFocus && !KeyboardInsets.animating)
            ensureVisible(edit, edit.cursorRectangle)
    }
    /// Keep what is being edited visible: the caret of the block, else the table cell.
    function ensureEditVisible() {
        if (KeyboardInsets.animating || !editor.alive)
            return
        if (activeEditor && activeEditor.activeFocus)
            ensureVisible(activeEditor, activeEditor.cursorRectangle)
        else if (editingTable && editingTable.cellInput)
            ensureVisible(editingTable.cellInput, Qt.rect(0, 0, editingTable.cellInput.width, editingTable.cellInput.height))
    }
    // The view shrinks with the keyboard (the shell lifts it): what is being edited stays
    // visible — once the keyboard has settled. (Growing, it covers nothing: the view stays where
    // the user scrolled it.)
    property real _lastHeight: 0
    onHeightChanged: {
        const shrank = height < _lastHeight - 0.5
        _lastHeight = height
        if (shrank && !autoHeight && (activeEditor !== null || editingTable !== null))
            later(() => editor.ensureEditVisible())
    }
    readonly property bool keyboardSliding: KeyboardInsets.animating
    onKeyboardSlidingChanged: {
        if (!keyboardSliding && KeyboardInsets.shown && (activeEditor !== null || editingTable !== null))
            later(() => editor.ensureEditVisible())
    }

    /// Edit cell (r, c) of the table in `row` (the cell's toolbar after an undo or redo: a table
    /// whose text changed is a new delegate, so it is looked up again).
    function editTableCell(row, r, c) {
        if (!controller || row < 0 || row >= controller.blocks.count)
            return
        list.forceLayout()
        const item = list.itemAtIndex(row) as BlockDelegate
        const table = item ? item.contentItem as TableBlockView : null
        if (table)
            table.startCell(r, c)
    }

    function releaseSourceKeyboard() {
        if (sourceLoader.item)
            (sourceLoader.item as SourceEditor).releaseKeyboard()
    }

    function activateLink(row, link) {
        if (!controller)
            return
        const r = controller.activateLink(row, link, false)
        if (!r || !r.action)
            return
        if (r.action === "reminder") {
            dialogs.editReminder(row, r.from, r.to)
        } else if (r.action === "scrollTo") {
            revealRow(r.row)
        } else if (r.action === "notFound") {
            noticeMissing(r.kind === "block" ? r.name : "", r.kind === "heading" ? r.name : "")
        }
    }

    /// Run the result of PageController.applySlashCommand / applyCompletion.
    function handleCommandResult(row, result) {
        if (!result)
            return
        switch (result.action) {
        case "reminder": dialogs.createReminder(row, false); break
        case "alarm": dialogs.createReminder(row, true); break
        case "due": dialogs.pickDate(row, "due"); break
        case "scheduled": dialogs.pickDate(row, "scheduled"); break
        case "recurrence": dialogs.pickRecurrence(row); break
        case "date": dialogs.pickJournalDate(row, result.cursor); break
        case "template": dialogs.pickTemplate(row); break
        case "image": dialogs.pickImage(row, result.cursor); break
        case "table":
            list.forceActiveFocus()
            break
        case "complete":
            later(() => editor.updateCompletion(editor.activeEditor))
            break
        }
    }

    // ---- Autocomplete ------------------------------------------------------------------------

    /// Re-detect the completion trigger at the caret of `edit` (InlineEditor). A word being
    /// composed by the input method (Android keyboards compose every word) counts as typed.
    function updateCompletion(edit) {
        if (!edit || !edit.activeFocus || edit.readOnly || !edit.completions) {
            closeSuggestions()
            return
        }
        const plain = edit.plain()
        const caret = edit.cursorPosition
        const pre = edit.preeditText
        const trigger = pre === "" ? suggestionModel.detect(plain, caret)
                                   : suggestionModel.detect(plain.slice(0, caret) + pre + plain.slice(caret), caret + pre.length)
        if (trigger.mode === "") {
            closeSuggestions()
            return
        }
        suggestionModel.setTrigger(trigger)
        if (suggestionModel.count === 0) {
            closeSuggestions()
            return
        }
        const popup = suggestionPopup()
        popup.trigger = trigger
        popup.row = edit.row
        popup.target = edit
        popup.show()
    }

    function acceptSuggestion(index) {
        const edit = suggestions ? suggestions.target : null
        if (!controller || !edit || index < 0 || index >= suggestionModel.count)
            return
        // The trigger counted the composed word: make it text before replacing it.
        const item = suggestionModel.get(index)
        if (EditorInput.commitPreedit(edit))
            updateCompletion(edit)
        const trigger = suggestions.trigger
        const row = suggestions.row
        closeSuggestions()
        const result = controller.applyCompletion(row, trigger, item)
        handleCommandResult(row, result)
    }

    // ---- Keyboard -----------------------------------------------------------------------------

    function isFirstLine(edit) {
        return edit.cursorRectangle.y < edit.lineHeightPx / 2
    }
    function isLastLine(edit) {
        return edit.cursorRectangle.y + edit.cursorRectangle.height > edit.contentHeight - edit.lineHeightPx / 2
    }

    /// `x`: the caret's x in the editor's coordinates (blocks are indented differently).
    function goToRow(row, edge, x) {
        if (row < 0)
            return
        if (controller.blocks.get(row).view === "table") {
            controller.stopEditing()
            controller.selectedRow = row
            list.forceActiveFocus()
            ensureRowVisible(row)
            return
        }
        navigationX = x
        navigationEdge = edge
        startEditing(row, edge === "top" ? 0 : -1)
    }

    /// Keys the editor handles itself (structure, navigation, shortcuts): a word the input
    /// method is still composing is committed before them, so Enter splits after it, Backspace
    /// at the "start" does not merge blocks while a word is being typed, and nothing is lost.
    function isCommandKey(event) {
        if ((event.modifiers & (Qt.ControlModifier | Qt.AltModifier)) !== 0)
            return true
        switch (event.key) {
        case Qt.Key_Return: case Qt.Key_Enter: case Qt.Key_Backspace: case Qt.Key_Delete:
        case Qt.Key_Tab: case Qt.Key_Backtab: case Qt.Key_Up: case Qt.Key_Down: case Qt.Key_Escape:
            return true
        }
        return false
    }

    /// Keys of an inline editor (InlineEditor.qml). Returns true when handled.
    function handleEditorKey(event, edit) {
        if (edit.preeditText !== "" && isCommandKey(event))
            EditorInput.commitPreedit(edit)
        const row = edit.row
        const ctrl = (event.modifiers & Qt.ControlModifier) !== 0
        const shift = (event.modifiers & Qt.ShiftModifier) !== 0
        const alt = (event.modifiers & Qt.AltModifier) !== 0
        const multiline = edit.multiline

        // Suggestion popup navigation.
        if (suggestionsOpen) {
            switch (event.key) {
            case Qt.Key_Up: suggestions.move(-1); return true
            case Qt.Key_Down: suggestions.move(1); return true
            case Qt.Key_Return:
            case Qt.Key_Enter:
            case Qt.Key_Tab:
                if (!ctrl && !shift) {
                    acceptSuggestion(suggestions.currentIndex)
                    return true
                }
                break
            case Qt.Key_Escape: closeSuggestions(); return true
            }
        }

        // A copied picture (or copied files) goes in as an attachment, not as text.
        if (((ctrl && !alt && !shift && event.key === Qt.Key_V) || (shift && !ctrl && event.key === Qt.Key_Insert))
                && !multiline && controller.clipboardHasImage()) {
            const pasted = controller.pasteImage(row, edit.selectionStart, edit.selectionEnd)
            if (pasted.row >= 0)
                return true
        }
        if (ctrl && !alt) {
            switch (event.key) {
            case Qt.Key_Z:
                if (shift) controller.redo(); else controller.undo()
                return true
            case Qt.Key_Y: controller.redo(); return true
            case Qt.Key_E: toggleSourceMode(); return true
            case Qt.Key_Period: controller.toggleFold(row); return true
            case Qt.Key_Return:
            case Qt.Key_Enter:
                // Ctrl+Shift+Enter: the status menu; Ctrl+Enter: the next status.
                if (shift)
                    openTaskStatusMenuFor(row)
                else
                    controller.cycleTask(row)
                return true
            case Qt.Key_C:
                // Ctrl+Shift+C: the block's reference ((id)); Ctrl+C copies the selected text.
                if (!shift)
                    return false
                copyCurrentLink("blockRef")
                return true
            }
            return false
        }
        if (alt && !ctrl) {
            if (event.key === Qt.Key_Up) { controller.moveUp(row); ensureRowVisible(controller.editingRow); return true }
            if (event.key === Qt.Key_Down) { controller.moveDown(row); ensureRowVisible(controller.editingRow); return true }
            return false
        }

        switch (event.key) {
        case Qt.Key_Return:
        case Qt.Key_Enter:
            if (shift || multiline) {
                edit.insertLineBreak()
                return true
            }
            if (edit.selectedText !== "")
                edit.remove(edit.selectionStart, edit.selectionEnd)
            controller.split(row, edit.cursorPosition)
            ensureRowVisible(controller.editingRow)
            return true
        case Qt.Key_Backspace:
            if (edit.cursorPosition === 0 && edit.selectedText === "") {
                if (multiline) {
                    if (edit.text === "") {
                        const focus = controller.deleteBlock(row)
                        if (focus >= 0)
                            startEditing(focus, -1)
                    }
                    return true
                }
                const info = controller.blocks.get(row)
                if (info.taskStatus !== "" && info.kind === "listItem") {
                    controller.removeTaskMarker(row)
                    return true
                }
                // A document's list item loses its marker (Obsidian), a nested one moves out.
                if (documentMode && info.kind === "listItem") {
                    const left = controller.leaveList(row)
                    if (left.row >= 0)
                        ensureRowVisible(left.row)
                    return true
                }
                const r = controller.mergeWithPrevious(row)
                if (r.row >= 0)
                    ensureRowVisible(r.row)
                return true
            }
            return false
        case Qt.Key_Delete:
            if (!multiline && edit.cursorPosition === edit.length && edit.selectedText === "") {
                controller.mergeNext(row)
                return true
            }
            return false
        case Qt.Key_Tab:
            if (multiline && !shift) {
                edit.insert(edit.cursorPosition, "\t")
                return true
            }
            // A document indents list items only; in a paragraph Tab does nothing (no outline).
            if (!documentMode || controller.blocks.get(row).kind === "listItem")
                controller.indent(row)
            return true
        case Qt.Key_Backtab:
            if (!documentMode || controller.blocks.get(row).kind === "listItem")
                controller.outdent(row)
            return true
        case Qt.Key_Up:
            if (!shift && isFirstLine(edit)) {
                goToRow(visualPrevious(row), "bottom", edit.mapToItem(editor, edit.cursorRectangle.x, 0).x)
                return true
            }
            return false
        case Qt.Key_Down:
            if (!shift && isLastLine(edit)) {
                goToRow(visualNext(row), "top", edit.mapToItem(editor, edit.cursorRectangle.x, 0).x)
                return true
            }
            return false
        case Qt.Key_Escape:
            stopEditing(true)
            return true
        }
        return false
    }

    /// A key reached the list while a block is being edited: its delegate was destroyed (scrolled
    /// far away). Bring the block back and give it the key, so typing is never silently lost.
    function forwardToEditingRow(event) {
        if (!controller || controller.editingRow < 0 || controller.sourceMode)
            return false
        if (activeEditor && activeEditor.activeFocus)
            return false
        // Typed text and keys that edit or move the caret; a lone modifier, PageUp/PageDown…
        // must not pull the view back.
        const printable = event.text !== "" && event.text.charCodeAt(0) >= 32 && event.text.charCodeAt(0) !== 127
        const editing = [Qt.Key_Return, Qt.Key_Enter, Qt.Key_Backspace, Qt.Key_Delete, Qt.Key_Tab, Qt.Key_Backtab,
                         Qt.Key_Left, Qt.Key_Right, Qt.Key_Up, Qt.Key_Down].indexOf(event.key) >= 0
        if (!printable && !editing)
            return false
        const row = controller.editingRow
        list.positionViewAtIndex(row, ListView.Contain)
        list.forceLayout()
        const target = activeEditor
        if (!target || !target.activeFocus || target.row !== row)
            return false
        EditorInput.sendKey(target, event.key, event.modifiers, event.text)
        return true
    }

    /// Text for a design-system component that shows it with Text.AutoText (BChip, BDialog
    /// subtitle, BToolTip; BBreadcrumb and BMenuItem show plain text): such text is rendered as styled text — images loaded, tags
    /// applied — when it looks like markup. Note text containing "<" is escaped, which AutoText
    /// then shows exactly as written.
    function autoTextSafe(s) {
        return s && s.indexOf("<") >= 0 ? TextHelper.escapeHtml(s) : (s || "")
    }

    /// Keys while a block is selected (not editing).
    function handleSelectionKey(event) {
        if (!controller)
            return false
        const row = controller.selectedRow
        const ctrl = (event.modifiers & Qt.ControlModifier) !== 0
        const shift = (event.modifiers & Qt.ShiftModifier) !== 0
        const alt = (event.modifiers & Qt.AltModifier) !== 0
        if (ctrl) {
            switch (event.key) {
            case Qt.Key_Z: if (shift) controller.redo(); else controller.undo(); return true
            case Qt.Key_Y: controller.redo(); return true
            case Qt.Key_E: toggleSourceMode(); return true
            case Qt.Key_Period: if (row >= 0) controller.toggleFold(row); return true
            case Qt.Key_Return:
            case Qt.Key_Enter:
                if (row >= 0 && shift)
                    openTaskStatusMenuFor(row)
                else if (row >= 0)
                    controller.cycleTask(row)
                return true
            case Qt.Key_C:
                if (!shift)
                    return false
                copyCurrentLink("blockRef") // nothing selected: says what to do
                return true
            }
            return false
        }
        if (row < 0) {
            if (event.key === Qt.Key_Down || event.key === Qt.Key_Up) {
                const first = event.key === Qt.Key_Down ? visualNext(-1) : controller.lastVisibleRow()
                controller.selectedRow = first
                ensureRowVisible(first)
                return true
            }
            return false
        }
        if (alt) {
            if (event.key === Qt.Key_Up) { controller.moveUp(row); ensureRowVisible(controller.selectedRow); return true }
            if (event.key === Qt.Key_Down) { controller.moveDown(row); ensureRowVisible(controller.selectedRow); return true }
            return false
        }
        switch (event.key) {
        case Qt.Key_Up: {
            const prev = visualPrevious(row)
            if (prev >= 0) { controller.selectedRow = prev; ensureRowVisible(prev) }
            return true
        }
        case Qt.Key_Down: {
            const next = visualNext(row)
            if (next >= 0) { controller.selectedRow = next; ensureRowVisible(next) }
            return true
        }
        case Qt.Key_Return:
        case Qt.Key_Enter:
            if (controller.blocks.get(row).view === "table") {
                const item = list.itemAtIndex(row) as BlockDelegate
                if (item)
                    item.focusContent()
                return true
            }
            startEditing(row, -1)
            return true
        case Qt.Key_Delete:
        case Qt.Key_Backspace: {
            const focus = controller.deleteBlock(row)
            controller.selectedRow = focus
            return true
        }
        case Qt.Key_Tab: controller.indent(row); return true
        case Qt.Key_Backtab: controller.outdent(row); return true
        case Qt.Key_Escape: controller.selectedRow = -1; return true
        }
        return false
    }

    /// Focus left an inline editor: flush the save when it left the editor entirely.
    function editorFocusLost() {
        later(() => {
            if (!editor.controller)
                return
            if (!editor.activeFocus && !editor.suggestionsOpen && !dialogs.anyOpen)
                editor.controller.save()
        })
    }

    onActiveFocusChanged: if (!activeFocus) editorFocusLost()

    // Leaving the app (Android may kill it in the background): a word the keyboard is still
    // composing becomes text and is saved with the page (PageController saves on suspend too,
    // possibly before this runs).
    Connections {
        target: Application
        function onStateChanged() {
            if (Application.state === Qt.ApplicationActive || !editor.activeEditor)
                return
            if (EditorInput.commitPreedit(editor.activeEditor) && editor.controller)
                editor.controller.save()
        }
    }

    Connections {
        target: editor.controller
        function onEditingRowChanged() {
            if (editor.controller.editingRow < 0)
                editor.closeSuggestions()
            else
                editor.ensureRowVisible(editor.controller.editingRow)
        }
        function onLoadingChanged() {
            if (!editor.controller.loading) {
                list.userMoved = false
                // The editor may be gone by then (a day of the journal stream scrolled away).
                editor.later(() => list.positionViewAtBeginning())
            }
        }
        function onSourceModeChanged() {
            if (!editor.controller.sourceMode)
                list.forceActiveFocus()
        }
        function onAttachmentFailed(name) {
            editor.notice(qsTr("Could not add “%1” to the notebook").arg(name), "error")
        }
    }

    // ---- The blocks --------------------------------------------------------------------------

    // ---- Conflict with changes made elsewhere ------------------------------------------------
    Loader {
        id: conflictBanner
        anchors.top: parent.top
        anchors.topMargin: active ? Theme.space2 : 0
        x: Math.max(Theme.space2, editor.contentX - Theme.space3)
        width: Math.min(editor.width - 2 * Theme.space2, editor.contentWidth + 2 * Theme.space3)
        active: editor.controller !== null && editor.controller.conflict
        height: active && item ? (item as Item).implicitHeight : 0
        sourceComponent: BCard {
            tone: "warning"
            padding: Theme.space3
            accessibleName: qsTr("Conflicting changes")
            contentItem: ColumnLayout {
                spacing: Theme.space2
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.space2
                    BIcon { name: "triangle-alert"; size: Theme.iconSize; color: Theme.warningText; Layout.alignment: Qt.AlignTop }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("This page was also changed elsewhere and the changes could not be merged automatically. Your version is kept and not saved until you decide.")
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.textSm
                        color: Theme.fg
                        wrapMode: Text.Wrap
                    }
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: Theme.space2
                    BButton {
                        size: "small"
                        variant: "primary"
                        iconName: "git-merge"
                        text: qsTr("Compare and merge…")
                        onClicked: editor.openMergeDialog()
                    }
                    BButton {
                        size: "small"
                        text: qsTr("Keep mine")
                        onClicked: editor.controller.keepLocal()
                    }
                    BButton {
                        size: "small"
                        text: qsTr("Use the other version")
                        onClicked: editor.controller.takeRemote()
                    }
                }
            }
        }
    }

    // Popups, dialogs and the drop hint are created when first needed (see EditorDialogs): every
    // page and every day of the journal stream has its own editor.
    property BDialog mergeDialog: null
    function openMergeDialog() {
        if (!mergeDialog)
            mergeDialog = mergeComponent.createObject(editor) as BDialog
        mergeDialog.open()
    }
    Component {
        id: mergeComponent
        BDialog {
            id: dialog
            parent: T.Overlay.overlay
            title: qsTr("Merge changes")
            iconName: "git-merge"
            preferredWidth: Theme.dialogWidthLg
            showFooter: false
            BMergeView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(560, editor.height * 0.7)
                ours: dialog.opened && editor.controller ? editor.controller.sourceText : ""
                theirs: dialog.opened && editor.controller ? editor.controller.conflictText : ""
                oursLabel: qsTr("This device")
                theirsLabel: qsTr("Other version")
                onResolved: (text) => {
                    editor.controller.resolveConflict(text)
                    dialog.close()
                }
            }
        }
    }

    ListView {
        id: list
        // Touch flicks at the finger's speed, and no jump when a drag catches a fling (Qt 6.11 on
        // Android: TouchFlick, docs/UI.md "Scrolling performance").
        TouchFlick.enabled: true
        anchors.top: conflictBanner.bottom
        anchors.topMargin: conflictBanner.active ? Theme.space2 : 0
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        visible: !editor.controller || !editor.controller.sourceMode
        focus: true
        model: editor.controller ? editor.controller.blocks : null
        reuseItems: false
        readonly property int rowCache: editor.autoHeight ? 0 : 1600
        cacheBuffer: rowCache
        // A touchpad scrolls without Flickable knowing its velocity (scroll phases), so ListView
        // filled its cache below the view only, and every row above was created at the view's top
        // edge — where a row that still grows (a query's results, a Layout's next polish) pushed
        // the text being read down (QQuickListView::viewportMoved, Qt 6.11). Setting the cache size
        // again makes the list fill it in both directions: done while it moves up without a drag
        // or a flick (those have a velocity). Rows created above the view grow upwards. (A cache
        // set to 0 from outside stays 0: the scroll probe's --probe-cache.)
        property real lastContentY: 0
        onContentYChanged: {
            if (rowCache > 0 && cacheBuffer > 0 && contentY < lastContentY - 0.5 && !dragging && !flicking)
                cacheBuffer = cacheBuffer === rowCache ? rowCache + 1 : rowCache
            lastContentY = contentY
        }
        // autoHeight: every row is in the range the list fills at once (synchronously, or with
        // the incubation of the item holding the editor) — none waits for the asynchronous cache.
        displayMarginEnd: editor.autoHeight ? 1000000 : 0
        boundsBehavior: Flickable.StopAtBounds
        keyNavigationEnabled: false
        // The toolbar docked on the editor's bottom edge covers the bottom of the list (the
        // keyboard is below the editor: the shell lifts the view above it).
        bottomMargin: editor.toolbarVisible && editor.dockHost === editor ? editor.toolbarHeight : 0
        // A block starting to be edited near the bottom must not stay hidden under it.
        onBottomMarginChanged: if (editor.toolbarVisible) editor.later(() => editor.ensureEditVisible())
        // No press delay: a Flickable that delays presses replays them on release without the
        // release for pointer handlers (Qt sends it only when an item grabbed the replayed press),
        // so a quick tap (< 90 ms) on block text, a link, a bullet or a fold arrow did nothing on
        // touch screens. Taps act on release anyway, and TextEdit moves the caret on release for
        // touch, so scrolling still never edits.
        pressDelay: 0
        Accessible.role: Accessible.List
        Accessible.name: editor.controller ? editor.controller.title : ""

        /// The user scrolled: header size changes no longer move the view to the top.
        property bool userMoved: false
        onMovementStarted: userMoved = true

        header: Column {
            width: list.width
            // The header (page title) gets its final height after the first layout.
            onHeightChanged: if (!list.userMoved) list.positionViewAtBeginning()
            Loader {
                width: parent.width
                active: editor.header !== null
                sourceComponent: editor.header
            }
            Loader {
                x: editor.contentX
                width: editor.contentWidth
                active: editor.controller !== null && editor.controller.zoomBlock >= 0
                // An inactive Loader keeps the implicit height of the item it had (Qt 6.11): hidden,
                // the Column leaves no room for it after zooming out.
                visible: active
                sourceComponent: ZoomBreadcrumb {
                    controller: editor.controller
                }
            }
        }

        delegate: BlockDelegate {
            editor: editor
        }

        footer: Item {
            width: list.width
            height: editor.bottomSpace
            TapHandler {
                onTapped: {
                    if (!editor.controller)
                        return
                    const row = editor.controller.appendEmptyBlock()
                    editor.startEditing(row, -1)
                }
            }
            HoverHandler { cursorShape: Qt.IBeamCursor }
        }

        Keys.onPressed: (event) => {
            if (editor.forwardToEditingRow(event) || editor.handleSelectionKey(event))
                event.accepted = true
        }

        // The block menu at the pointer: a right-click anywhere on a block — its text, a heading,
        // a code, table or query frame, an embed — with a mouse, touchpad or stylus, never a finger
        // (Qt hands touch points to a TapHandler whatever its buttons; a long press on text is left
        // for selecting it). One handler for the list, not one per block. Bullets, numbers and
        // checkboxes take their own right-click (their menu at them); the page properties have none.
        // (Declared in the list, the handler belongs to its content item: scene coordinates.)
        TapHandler {
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
            acceptedButtons: Qt.RightButton
            onTapped: (eventPoint) => {
                if (!editor.controller)
                    return
                const scene = eventPoint.scenePosition
                const p = list.contentItem.mapFromItem(null, scene.x, scene.y)
                const row = list.indexAt(p.x, p.y)
                if (row < 0 || editor.controller.blocks.get(row).view === "frontmatter")
                    return
                const at = list.mapFromItem(null, scene.x, scene.y)
                editor.openBlockMenu(row, list, at.x, at.y)
            }
        }

        // The header (page title) gets its height after the first layout: start at the top.
        // (autoHeight: no scroll bar, the list never scrolls.)
        Component.onCompleted: {
            if (!editor.autoHeight)
                T.ScrollBar.vertical = scrollBarComponent.createObject(list)
            editor.later(() => list.positionViewAtBeginning())
        }
        Component {
            id: scrollBarComponent
            BScrollBar {}
        }
    }

    // ---- Files dropped on the page: attachments ------------------------------------------------
    DropArea {
        id: fileDrop
        anchors.fill: list
        enabled: editor.controller !== null && !editor.controller.sourceMode
        keys: ["text/uri-list"]
        onEntered: (drag) => drag.accepted = drag.hasUrls
        onDropped: (drop) => {
            if (!drop.hasUrls || !editor.controller)
                return
            const p = list.contentItem.mapFromItem(fileDrop, drop.x, drop.y)
            const row = list.indexAt(editor.contentX + 1, p.y)
            const c = editor.controller
            if (row >= 0 && row === c.editingRow && editor.activeEditor && editor.activeEditor.row === row)
                c.insertFiles(row, editor.activeEditor.selectionStart, editor.activeEditor.selectionEnd, drop.urls)
            else
                c.insertFilesAfter(row, drop.urls)
            drop.acceptProposedAction()
        }
    }
    Loader {
        // While files are dragged over the page.
        active: fileDrop.containsDrag
        anchors.fill: list
        anchors.margins: Theme.space2
        z: 5
        sourceComponent: Rectangle {
            radius: Theme.radiusLg
            color: Theme.alpha(Theme.info, 0.06)
            border.width: 2
            border.color: Theme.alpha(Theme.focusRing, 0.6)
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                y: Theme.space4
                width: dropLabel.implicitWidth + 2 * Theme.space4
                height: dropLabel.implicitHeight + 2 * Theme.space2
                radius: height / 2
                color: Theme.bgElevated
                border.width: 1
                border.color: Theme.border
                Row {
                    id: dropLabel
                    anchors.centerIn: parent
                    spacing: Theme.space2
                    BIcon { name: "image"; size: Theme.iconSizeSm; color: Theme.fgMuted; anchors.verticalCenter: parent.verticalCenter }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Drop to add to this page")
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.textSm
                        color: Theme.fg
                    }
                }
            }
        }
    }

    Loader {
        id: sourceLoader
        anchors.fill: parent
        active: editor.controller !== null && editor.controller.sourceMode
        sourceComponent: SourceEditor {
            editor: editor
            controller: editor.controller
        }
        onLoaded: (item as SourceEditor).focusEditor()
    }

    // ---- Popups, dialogs, toolbar ------------------------------------------------------------

    SuggestionModel {
        id: suggestionModel
        host: editor.controller ? editor.controller.host : null
        locale: DatesHelper.locale
        now: DatesHelper.fixedNow
        currentPath: editor.controller ? editor.controller.path : ""
        limit: 12
    }

    /// The autocomplete popup (tests read it), created the first time there is something to
    /// complete — like the dialogs and the touch toolbar, not with every editor.
    property SuggestionPopup suggestions: null
    readonly property bool suggestionsOpen: suggestions !== null && suggestions.visible
    function suggestionPopup(): SuggestionPopup {
        if (!suggestions)
            suggestions = suggestionComponent.createObject(editor) as SuggestionPopup
        return suggestions
    }
    function closeSuggestions() {
        if (suggestions)
            suggestions.close()
    }
    Component {
        id: suggestionComponent
        SuggestionPopup {
            editor: editor
            model: suggestionModel
            onActivated: (index) => editor.acceptSuggestion(index)
        }
    }

    // ---- The block menu --------------------------------------------------------------------------
    /// BlockMenu for a block (made when first opened, one per editor; tests read it): a bullet's or
    /// number's right-click / long press, a right-click on a block, the touch toolbar's "Więcej".
    property BlockMenu blockMenu: null
    function openBlockMenu(row, item, x, y) {
        if (!controller || row < 0 || row >= controller.blocks.count)
            return
        if (!blockMenu)
            blockMenu = blockMenuComponent.createObject(editor) as BlockMenu
        closeSuggestions()
        // A word the keyboard is still composing becomes text before anything acts on the block.
        if (activeEditor)
            EditorInput.commitPreedit(activeEditor)
        blockMenu.openFor(row)
        // Above the docked toolbar and the keyboard (it flips above its item when needed).
        blockMenu.bottomMargin = menuBottomMargin()
        if (item)
            blockMenu.popup(item, x, y)
        else
            blockMenu.popup()
    }
    /// The touch toolbar's "Więcej": the menu of the block being edited, right above the button.
    function openBlockMenuAbove(row, item) {
        if (!controller || row < 0)
            return
        if (!blockMenu)
            blockMenu = blockMenuComponent.createObject(editor) as BlockMenu
        blockMenu.openFor(row)
        openBlockMenu(row, item, 0, -blockMenu.openHeight() - Theme.space1)
    }
    Component {
        id: blockMenuComponent
        BlockMenu {
            editor: editor
        }
    }

    // ---- The status menu of a block's task ----------------------------------------------------
    /// TaskStatusMenu for a block: its checkbox's right-click / long press, Ctrl+Shift+Enter, the
    /// touch toolbar's task button held down. Created when first opened. The block is tracked while
    /// the menu is open (sync or undo may move it); the choice is one undo step
    /// (PageController.setTaskStatus) and the block being edited gets the keyboard back.
    property TaskStatusMenu taskMenu: null
    property int taskMenuBlock: -1
    /// Where the keyboard goes when the menu closes: "edit" (the block being edited), "list" (the
    /// selected block, Ctrl+Shift+Enter) or "" (opened with the pointer: wherever it was).
    property string taskMenuReturn: ""
    function openTaskStatusMenu(row, item, x, y) {
        if (!controller || row < 0 || row >= controller.blocks.count)
            return
        if (!taskMenu)
            taskMenu = taskMenuComponent.createObject(editor) as TaskStatusMenu
        // Above the docked toolbar and the keyboard (it flips above its item when needed).
        taskMenu.bottomMargin = menuBottomMargin()
        if (taskMenuBlock >= 0)
            controller.untrack(taskMenuBlock)
        taskMenuBlock = controller.trackBlock(row)
        // The block being edited (this one or another) keeps its editing when the menu closes.
        const editing = controller.editingRow >= 0 && activeEditor !== null && activeEditor.row === controller.editingRow
                        && activeEditor.activeFocus
        taskMenuReturn = editing ? "edit" : list.activeFocus ? "list" : ""
        taskMenu.status = taskStatusOf(row)
        closeSuggestions()
        // A word the keyboard is still composing becomes text first (the status resets the text).
        if (editing)
            EditorInput.commitPreedit(activeEditor)
        if (item)
            taskMenu.popup(item, x, y)
        else
            taskMenu.popup()
    }
    /// Can the block get a task status (a list item or a paragraph)? A heading, code, a table, a
    /// quote… cannot: the keyboard and the toolbar open no menu whose choices would do nothing.
    function canHaveTaskStatus(row) {
        if (!controller || row < 0 || row >= controller.blocks.count)
            return false
        const kind = controller.blocks.get(row).kind
        return kind === "listItem" || kind === "paragraph"
    }
    /// Ctrl+Shift+Enter: the menu under the caret of the block being edited, else under the
    /// selected block's first line.
    function openTaskStatusMenuFor(row) {
        if (!canHaveTaskStatus(row))
            return
        const edit = activeEditor
        const item = list.itemAtIndex(row) as BlockDelegate
        if (edit && edit.row === row && edit.active) {
            const r = edit.cursorRectangle
            openTaskStatusMenu(row, edit, r.x, r.y + r.height)
        } else if (item) {
            openTaskStatusMenu(row, item, item.contentLeft, item.firstLineCenter + lineHeight / 2)
        } else {
            openTaskStatusMenu(row, null, 0, 0)
        }
        if (taskMenu)
            taskMenu.highlightStatus()
    }
    /// The touch toolbar's task button held down: the menu right above the button (the toolbar
    /// is docked on the keyboard).
    function openTaskStatusMenuAbove(row, item) {
        if (!canHaveTaskStatus(row))
            return
        if (!taskMenu)
            taskMenu = taskMenuComponent.createObject(editor) as TaskStatusMenu
        openTaskStatusMenu(row, item, 0, -taskMenu.openHeight() - Theme.space1)
    }
    /// The status of a block's task as the menu shows it ("" = none).
    function taskStatusOf(row) {
        const info = controller.blocks.get(row)
        return info.kind === "listItem" ? info.taskStatus : ""
    }
    function chooseTaskStatus(status) {
        if (!controller || taskMenuBlock < 0)
            return
        const row = controller.trackedRow(taskMenuBlock)
        controller.untrack(taskMenuBlock)
        taskMenuBlock = -1
        if (row >= 0 && status !== taskStatusOf(row))
            controller.setTaskStatus(row, status)
    }
    Component {
        id: taskMenuComponent
        TaskStatusMenu {
            onStatusChosen: (status) => editor.chooseTaskStatus(status)
            // The menu had the keyboard: back to the block's text (the caret where it was) or to
            // the selected block.
            onClosed: {
                const back = editor.taskMenuReturn
                editor.taskMenuReturn = ""
                if (back === "edit" && editor.activeEditor && editor.controller
                        && editor.activeEditor.row === editor.controller.editingRow)
                    editor.activeEditor.ensureFocus()
                else if (back === "list")
                    list.forceActiveFocus()
            }
        }
    }

    EditorDialogs {
        id: dialogs
        editor: editor
        controller: editor.controller
    }

    // The touch toolbar on the bottom edge of the dock host (the shell keeps it on the keyboard;
    // a day of the journal stream docks it on the stream's frame, so it never scrolls with the
    // day). Touch UIs, from the first edit on (then kept: not created again for every edit).
    // Latched here, not in `active`'s own binding: the Loader loads synchronously inside that
    // binding, so an onLoaded that changed one of its inputs was a binding loop. The Loader is
    // the editor's (destroyed with it) even when it is shown in another item.
    onToolbarVisibleChanged: if (toolbarVisible) toolbarLoader.active = true
    Component.onCompleted: if (toolbarVisible) toolbarLoader.active = true
    Loader {
        id: toolbarLoader
        objectName: "editToolbarSlot"
        parent: editor.dockHost
        active: false
        visible: editor.toolbarVisible
        y: editor.dockHost.height - height
        width: editor.dockHost.width
        height: editor.toolbarHeight
        z: 10
        sourceComponent: MobileEditToolbar {
            editor: editor
        }
    }

    /// The dialogs (for tests and the playground's scripted states).
    readonly property alias dialogs: dialogs
    readonly property alias listView: list
}
