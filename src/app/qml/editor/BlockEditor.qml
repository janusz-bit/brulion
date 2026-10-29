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
// (core InlineRenderer, links page:/tag:/block:/ext:/task:/reminder:/date:), the block being edited a
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
// `message(text, kind, action)` (emitted by notice()) tells what the host should show as a toast;
// `action` — { text, run } or null — is its button (deleting or cutting blocks: "Cofnij").
// Block selection (docs/UI.md "Selecting and copying"; PageController keeps it as two uids):
// Shift+↑/↓ (also from the first / last line of an edited block), Ctrl+A twice, Shift+click, a
// mouse drag from one block into another (the list no longer scrolls with a left drag on non-touch
// UIs; it scrolls near its edges while selecting), the block menu's "Zaznacz"; Ctrl+click picks
// single blocks into or out of it (gaps allowed); on touch UIs a long press on a block's text — the
// selection mode: taps extend or shrink (with the tools' "Pojedynczo" on: pick single blocks), two
// grips adjust the ends, the selection tools are docked where the editing toolbar is ("Gotowe" and
// Back leave). Ctrl+C / Ctrl+X / Ctrl+V / Delete / Ctrl+Enter act on the whole selection, Tab and
// Alt+↑/↓ on one without gaps (else a toast says why not); desktops show a
// compact SelectionToolbar at the top when two or more blocks are selected. A day of the journal
// stream (`stream`, StreamSelection) selects across days too: Shift+click, Shift+↑/↓ past its edge,
// a drag into another day, a tap in the touch selection mode; such a selection is only copied
// (spanRefuses() tells so for the other tools). In an edited block
// Ctrl+C / Ctrl+X copy its exact characters and a right-click opens Wytnij / Kopiuj / Wklej / Zaznacz
// wszystko.
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
    readonly property int paragraphGap: Math.round(fontSize * (Theme.dense ? 0.6 : 0.75))
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
                                          : editingTable !== null ? "cell"
                                          : (inSpan ? spanTools : selectionMode && selectionCount > 0) ? "selection" : ""
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

    /// Something to tell the user (a toast): kind "error" | "info" | "success"; `action` — { text,
    /// run } or null — a button on the toast (after deleting or cutting blocks: "Cofnij",
    /// undoAction()).
    signal message(string text, string kind, var action)
    /// Say `text` (message()); `action` is optional.
    function notice(text, kind, action) {
        message(text, kind, action ? action : null)
    }
    /// The "Cofnij" of a toast about the edit just made (`before`: controller.undoToken() before
    /// it; null when it changed nothing): undoes that edit while it is still the last step of this
    /// page's history (PageController.undoStep) — after other edits, on another page or once undone
    /// it says so instead.
    function undoAction(before) {
        const ctl = controller
        if (!ctl)
            return null
        const token = ctl.undoToken()
        if (token === before)
            return null
        const path = ctl.path
        return {
            text: qsTr("Undo"),
            run: () => {
                try {
                    if (ctl.path === path && ctl.undoStep(token))
                        return
                    if (editor.controller === ctl)
                        editor.notice(qsTr("Not undone: the page has changed since"), "info")
                } catch (e) {
                    // The page (or the whole editor) is gone: nothing to undo here.
                }
            }
        }
    }

    // ---- Block selection (docs/UI.md "Selecting and copying") -----------------------------------
    /// Touch UIs: the selection mode (a long press on a block's text, "Zaznacz" in the block menu or
    /// the editing toolbar): taps extend or shrink the selection instead of editing, grips at the
    /// band's ends adjust it, the selection tools are docked like the editing toolbar ("selection"
    /// toolbarMode); "Gotowe" and Back leave it. Ends with the selection or when editing starts.
    property bool selectionMode: false
    /// The selection mode's "Pojedynczo" (one by one): taps pick single blocks into or out of the
    /// selection (PageController.toggleSelected) instead of moving its end. Off again when the
    /// selection mode ends.
    property bool pickMode: false
    onSelectionModeChanged: if (!selectionMode) pickMode = false
    /// The band's grips are shown (the touch selection mode, its blocks without gaps, not picking
    /// one by one).
    readonly property bool selectionGrips: selectionMode && touch && !pickMode && controller !== null
                                           && controller.selectionContiguous && !inSpan
    readonly property int selectionCount: controller !== null ? controller.selectionCount : 0
    /// Why a selection with gaps between its blocks is not indented or moved (a toast, the tools'
    /// tooltips).
    readonly property string gapReason: qsTr("Only blocks selected without gaps can be indented or moved")
    /// A day of the journal stream: the stream's selection over several days (null elsewhere).
    property StreamSelection stream: null
    /// Blocks of several days are selected (this day may hold a part of them, or none).
    readonly property bool inSpan: stream !== null && stream.active
    /// This editor shows the tools of the selection over several days.
    readonly property bool spanTools: inSpan && stream.toolsController === controller
    /// Why a tool does nothing on a selection over several days (a toast, the tools' tooltips).
    readonly property string spanReason: qsTr("Only copying works across several days")
    /// A selection over several days holds the selection: say that only copying works there. True
    /// when it did (the caller does nothing else).
    function spanRefuses() {
        if (!inSpan)
            return false
        notice(spanReason, "info")
        return true
    }
    /// The end of a selection over several days moved into this day (StreamSelection.focusMoved):
    /// the keys come here, a touch UI is in the selection mode, `reveal` scrolls to `row`.
    function spanFocusHere(row, reveal) {
        if (!controller)
            return
        closeSuggestions()
        if (touch) {
            Qt.inputMethod.hide()
            selectionMode = true
        }
        if (!list.activeFocus)
            list.forceActiveFocus()
        if (reveal)
            ensureRowVisible(row)
    }

    // The title above the properties panel (BlockDelegate trades their places on screen): set by
    // those two delegates.
    readonly property bool titleFirst: controller !== null && controller.titleRow > 0 && controller.zoomBlock < 0
    property real titleHeight: 0
    property real propertiesHeight: 0
    /// The zoomed row the view last followed (onZoomChanged in the controller's Connections).
    property int shownZoom: -1

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
        // contentHeight and font read for real: positionToRectangle() is a method call, not
        // tracked, and a bare `void probe.font` is compiled away with the dependency.
        readonly property real firstBaselineY: {
            if (probe.contentHeight < 0 || probe.font.pixelSize === 0)
                return probeMetrics.ascent
            return probe.positionToRectangle(0).y + probeMetrics.ascent
        }
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
        shownZoom = controller.zoomBlock
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
            // (autoHeight: the list never scrolls — the journal stream does, below.)
            list.positionViewAtIndex(row, ListView.Center)
            // The row's delegate exists once the list has been laid out there.
            list.forceLayout()
            const item = list.itemAtIndex(row) as BlockDelegate
            if (!item)
                return
            if (editor.scroller)
                editor.scroller.ensureVisible(item, Qt.rect(0, 0, item.width, item.height))
            item.flash()
        })
        list.forceActiveFocus()
    }
    /// A link's block (`blockId`) or heading is not on this page (nor anywhere): say so. `ref`: the
    /// block was named by a ((reference)), which no page of the notebook has.
    function noticeMissing(blockId, heading, ref) {
        if (blockId && ref)
            notice(qsTr("No block in the notebook has the id %1").arg(blockId), "info")
        else if (blockId)
            notice(qsTr("Block ^%1 is not on this page").arg(blockId), "info")
        else if (heading)
            notice(qsTr("Heading “%1” is not on this page").arg(heading), "info")
    }
    /// noticeMissing() for PageController.activateLink's {action: "notFound", kind, name}.
    function noticeNotFound(r) {
        noticeMissing(r.kind === "heading" ? "" : r.name, r.kind === "heading" ? r.name : "", r.kind === "ref")
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
        else if (editingTable && editingTable.block && editingTable.block.index === row && editingTable.cellInput)
            EditorInput.commitPreedit(editingTable.cellInput)
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
    /// The current block: the one being edited — a block, or the table whose cell is being edited
    /// —, else the selected one; -1 when none.
    function currentRow() {
        if (!controller)
            return -1
        if (controller.editingRow >= 0)
            return controller.editingRow
        if (editingTable && editingTable.block)
            return editingTable.block.index
        return controller.selectedRow
    }
    /// The command palette and Ctrl+Shift+C: a link to the page ("page"), to the current block
    /// (currentRow()) ("blockRef", "blockLink") or to the heading of its section ("heading": the
    /// last heading at or above it, as the side panel's outline marks).
    function copyCurrentLink(kind) {
        if (!controller)
            return false
        if (kind === "page")
            return copyLink(-1, "page")
        if (controller.sourceMode) {
            notice(copyFailure("sourceMode"), "info")
            return false
        }
        const current = currentRow()
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

    // ---- Selecting and copying blocks (docs/UI.md "Selecting and copying") ------------------------

    /// Stop editing (the word being composed kept, the keyboard's suggestions closed) before a
    /// block selection takes over; the list gets the keyboard.
    function leaveEditingForSelection() {
        if (activeEditor)
            EditorInput.commitPreedit(activeEditor)
        closeSuggestions()
        if (controller && controller.editingRow >= 0)
            controller.stopEditing()
        list.forceActiveFocus()
    }
    /// Select `row` (with its subtree) and, on touch UIs, enter the selection mode (the keyboard
    /// goes away, the selection tools dock where the editing toolbar was).
    function selectBlock(row) {
        if (!controller || row < 0 || row >= controller.blocks.count)
            return
        leaveEditingForSelection()
        if (touch)
            Qt.inputMethod.hide()
        controller.selectedRow = row
        selectionMode = touch && controller.selectionCount > 0
        ensureRowVisible(row)
    }
    function leaveSelectionMode() {
        selectionMode = false
        if (inSpan)
            stream.clear() // every day's part
        else if (controller)
            controller.clearSelection()
    }
    /// A tap or click that would start editing `row` (its text, a code or table frame, a query, an
    /// embed, a rule, a bullet or number): Shift+click (`modifiers`, else the event's own:
    /// EditorInput.keyboardModifiers()) selects from the edited or selected block to it, Ctrl+click
    /// picks it into or out of the selection; in the touch selection mode a tap moves the
    /// selection's end there (with "Pojedynczo": picks it). True when it was taken.
    function blockTapped(row, modifiers) {
        // The press that ended a drag selection is no tap (its release may be on a block).
        if (drag.selected)
            return true
        if (!controller || row < 0)
            return false
        const mods = modifiers !== undefined ? modifiers : EditorInput.keyboardModifiers()
        if (!touch && (mods & Qt.ShiftModifier)) {
            extendSelectionTo(row)
            return true
        }
        if (!touch && (mods & Qt.ControlModifier)) {
            toggleBlock(row)
            return true
        }
        // Touch: the selection mode of another day (or a selection over several days) — a tap here
        // moves the selection's end into this day. A span none of whose days is on screen any more
        // shows no tools: a tap edits (and ends it), as after scrolling away from one day's selection.
        const spanTap = touch && stream !== null
                        && (stream.active ? stream.toolsController !== null : stream.othersSelected(controller))
        if (!selectionMode && !spanTap)
            return false
        if (pickMode && !inSpan)
            toggleBlock(row)
        else if (controller.blocks.get(row).view !== "frontmatter"
                 && !(stream !== null && stream.extendTo(controller, row)))
            controller.extendSelectionTo(row)
        return true
    }
    /// Ctrl+click on `row` (a tap with "Pojedynczo"): that block with its children into the
    /// selection or out of it, the rest stays (gaps allowed); the block being edited joins it.
    function toggleBlock(row) {
        if (!controller || row < 0 || row >= controller.blocks.count || controller.blocks.get(row).view === "frontmatter")
            return
        if (activeEditor)
            EditorInput.commitPreedit(activeEditor)
        closeSuggestions()
        const result = controller.toggleSelected(row)
        if (result === "inParent")
            notice(qsTr("This block is selected with the block it belongs to"), "info")
        if (!list.activeFocus)
            list.forceActiveFocus()
        if (touch && controller.selectionCount > 0) {
            Qt.inputMethod.hide()
            selectionMode = true
        }
    }
    /// Indenting or moving the selection: only without gaps (blocks picked one by one may leave
    /// some) — else a toast says why. True when it may go on.
    function selectionMovable() {
        if (!controller || controller.selectionContiguous)
            return true
        notice(gapReason, "info")
        return false
    }
    /// Shift+click on `row`: the selection from the block being edited or the selection's anchor
    /// to it (a block alone when there is neither).
    function extendSelectionTo(row) {
        if (!controller || row < 0)
            return
        // From a block selected or edited in another day of the journal stream (a selection over
        // several days), or back into the day its anchor is in.
        if (stream !== null && controller.blocks.get(row).view !== "frontmatter" && stream.extendTo(controller, row))
            return
        const anchor = controller.selectionAnchorRow >= 0 ? controller.selectionAnchorRow : controller.editingRow
        leaveEditingForSelection()
        controller.selectRange(anchor >= 0 ? anchor : row, row)
        ensureRowVisible(row)
    }
    /// Ctrl+A with blocks: every block of the page (in a zoom the zoomed subtree).
    function selectAllBlocks() {
        if (!controller)
            return
        if (inSpan)
            stream.clear() // this day's blocks only
        leaveEditingForSelection()
        controller.selectAll()
        if (touch && controller.selectionCount > 0)
            selectionMode = true
    }
    /// Why a copy copied nothing (PageController.copySelection's reason).
    function selectionCopyFailure(reason) {
        switch (reason) {
        case "tooLarge": return qsTr("The selection is too large to copy. Select fewer blocks.")
        case "empty": return qsTr("Nothing to copy")
        default: return copyFailure(reason)
        }
    }
    /// Copy the selection (Markdown with HTML, or `plainText`) and say so. True when copied.
    function copySelection(plainText) {
        if (!controller)
            return false
        const r = inSpan ? stream.copy(plainText === true) : controller.copySelection(plainText === true)
        if (r.ok && inSpan)
            notice(qsTr("Copied %n block(s) from several days", "", r.count), "success")
        else if (r.ok)
            notice(qsTr("Copied %n block(s)", "", r.count), "success")
        else
            notice(selectionCopyFailure(r.reason), r.reason === "empty" ? "info" : "error")
        return r.ok
    }
    function cutSelection() {
        if (!controller || spanRefuses())
            return false
        const before = controller.undoToken()
        const r = controller.cutSelection()
        if (r.ok) {
            notice(qsTr("Cut %n block(s)", "", r.count), "success", undoAction(before))
            if (controller.selectedRow >= 0)
                ensureRowVisible(controller.selectedRow)
        } else {
            notice(selectionCopyFailure(r.reason), r.reason === "empty" ? "info" : "error")
        }
        return r.ok
    }
    function deleteSelection() {
        if (!controller || spanRefuses())
            return
        const count = Math.max(1, controller.selectionCount)
        const multiple = controller.multiSelection
        const before = controller.undoToken()
        const focus = controller.deleteSelection()
        if (multiple)
            notice(qsTr("Deleted %n block(s)", "", count), "info", undoAction(before))
        if (focus >= 0)
            ensureRowVisible(focus)
    }
    /// Ctrl+V with blocks selected: the clipboard's text replaces them.
    function pasteOverSelection() {
        if (!controller || spanRefuses())
            return
        const r = controller.pasteOverSelection()
        if (r.row < 0) {
            notice(qsTr("Nothing to paste"), "info")
        } else {
            ensureRowVisible(r.row)
            offerPastedPictures(r)
        }
    }
    /// Copy the exact characters [from, to) of the edited block `edit` (Ctrl+C, Ctrl+X, the text
    /// menu: TextEdit's own copy turns no-break spaces into spaces); `cut` removes them.
    function copyEditSelection(edit, cut) {
        if (!controller || !edit || edit.selectionStart === edit.selectionEnd)
            return false
        const from = edit.selectionStart
        const to = edit.selectionEnd
        const r = controller.copyEditText(edit.row, from, to, cut === true)
        if (!r.ok) {
            notice(selectionCopyFailure(r.reason), "error")
            return false
        }
        if (cut)
            edit.remove(from, to)
        return true
    }
    /// Paste into the edited block `edit` at its selection: Markdown splits into blocks, HTML from
    /// other apps comes as Markdown (PageController.pasteClipboard). False when the clipboard has
    /// no text (then nothing happens).
    function pasteIntoEdit(edit) {
        if (!controller || !edit)
            return false
        if (edit.preeditText !== "")
            EditorInput.commitPreedit(edit)
        const r = controller.pasteClipboard(edit.row, edit.selectionStart, edit.selectionEnd)
        if (r.row < 0)
            return false
        offerPastedPictures(r)
        return true
    }
    /// Pictures from the internet a paste brought (PageController.pasteClipboard): a picture's
    /// address alone (a browser's "Kopiuj adres obrazu") — "Zapisz w notatniku" makes it the
    /// picture and stores it in the notebook (urlToPicture); pictures of pasted HTML — stored
    /// on "Zapisz w notatniku" (downloadWebPictures). Offered on a toast; nothing is fetched before.
    function offerPastedPictures(r) {
        const ctl = controller
        if (!ctl || !r)
            return
        const path = ctl.path
        const samePage = () => controller === ctl && ctl.path === path
        if (r.pictureUrl) {
            const row = r.row, from = r.pictureFrom, to = r.pictureTo, url = r.pictureUrl
            notice(qsTr("Picture address pasted"), "info", {
                text: qsTr("Save to notebook"),
                run: () => {
                    if (!samePage() || ctl.urlToPicture(row, from, to, url, true).row < 0)
                        notice(qsTr("Not changed: the address was edited since"), "info")
                }
            })
        } else if (r.webPictures && r.webPictures.length > 0) {
            const urls = r.webPictures
            notice(qsTr("Pasted %n picture(s) from the internet", "", urls.length), "info", {
                text: qsTr("Save to notebook"),
                run: () => {
                    if (!samePage()) {
                        notice(qsTr("Open that page again to save its pictures"), "info")
                        return
                    }
                    for (let i = 0; i < urls.length; ++i)
                        downloadWebPictures(urls[i])
                }
            })
        }
    }

    /// The row drawn at (x, y) of the list's content item; -1 when none (the header, the footer, a
    /// gap). Not ListView.indexAt(): it goes by the rows' layout and ignores the Translate that
    /// draws the page's title above its properties (titleFirst); mapping into each delegate goes
    /// through it. (Nor contentItem.childAt(): the list's own highlight item covers the current row.)
    function rowAt(x, y) {
        const children = list.contentItem.children
        for (let i = children.length - 1; i >= 0; --i) {
            const item = children[i] as BlockDelegate
            if (item && item.visible && item.contains(item.mapFromItem(list.contentItem, x, y)))
                return item.index
        }
        return -1
    }

    /// A click on the rendered text of `row` at (x, y) in that text's coordinates (`modifiers`: the
    /// click's, see blockTapped()).
    function editAt(row, x, y, modifiers) {
        if (blockTapped(row, modifiers))
            return
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

    /// Undo (`redo` false) or redo a step from the editor (keys, the toolbar). A step that brings a
    /// block selection back ends the editing of another block (PageController.restore): the keys
    /// go to the selection, and on touch UIs its tools replace the keyboard.
    function undoStep(redo) {
        if (!controller)
            return
        if (redo)
            controller.redo()
        else
            controller.undo()
        if (controller.editingRow >= 0 || controller.selectionCount === 0)
            return
        closeSuggestions()
        if (touch) {
            Qt.inputMethod.hide()
            selectionMode = true
        }
        if (!list.activeFocus)
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
            noticeNotFound(r)
        }
    }

    /// A new drawing at the caret `cursor` of `row` (-1: a block of its own after it), opened in the
    /// drawing editor (Rysunek: the slash command, the touch toolbar, the block menu).
    function insertDrawing(row, cursor) {
        if (!controller)
            return
        const r = controller.insertDrawing(row, cursor, cursor)
        if (r.path)
            controller.openDrawing(r.path)
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
        case "webImage": dialogs.pickWebImage(row, result.cursor); break
        case "drawing": insertDrawing(row, result.cursor); break
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
        const pasteKey = (ctrl && !alt && !shift && event.key === Qt.Key_V) || (shift && !ctrl && event.key === Qt.Key_Insert)
        if (pasteKey && !multiline && controller.clipboardHasImage()) {
            const pasted = controller.pasteImage(row, edit.selectionStart, edit.selectionEnd)
            if (pasted.row >= 0)
                return true
        }
        // Text: Markdown splits into blocks, HTML from other apps comes as Markdown, ids that
        // resolve elsewhere are dropped (PageController.pasteClipboard). Code and other multi-line
        // blocks take the text as it is (TextEdit's own paste).
        if (pasteKey && !multiline && pasteIntoEdit(edit))
            return true
        // Ctrl+Insert / Shift+Delete: copy / cut, exactly (like Ctrl+C / Ctrl+X).
        if (!alt && edit.selectionStart !== edit.selectionEnd
                && ((ctrl && !shift && event.key === Qt.Key_Insert) || (shift && !ctrl && event.key === Qt.Key_Delete))) {
            copyEditSelection(edit, event.key === Qt.Key_Delete)
            return true
        }
        if (ctrl && !alt) {
            switch (event.key) {
            case Qt.Key_Z: undoStep(shift); return true
            case Qt.Key_Y: undoStep(true); return true
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
                // Ctrl+Shift+C: the block's reference ((id)); Ctrl+C copies the selected text
                // exactly (TextEdit's own copy turns no-break spaces into spaces).
                if (shift) {
                    copyCurrentLink("blockRef")
                    return true
                }
                if (edit.selectionStart === edit.selectionEnd)
                    return false
                copyEditSelection(edit, false)
                return true
            case Qt.Key_X:
                if (shift || edit.selectionStart === edit.selectionEnd)
                    return false
                copyEditSelection(edit, true)
                return true
            case Qt.Key_A:
                // Notion's second Ctrl+A: the whole block's text is selected already (or there is
                // none): every block of the page.
                if (shift)
                    return false
                if (edit.length === 0 || (edit.selectionStart === 0 && edit.selectionEnd === edit.length))
                    selectAllBlocks()
                else
                    edit.selectAll()
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
            // Shift+↑ on the first line: the text selection to the block's start, then the blocks.
            if (shift && isFirstLine(edit))
                return extendFromEdit(edit, -1)
            return false
        case Qt.Key_Down:
            if (!shift && isLastLine(edit)) {
                goToRow(visualNext(row), "top", edit.mapToItem(editor, edit.cursorRectangle.x, 0).x)
                return true
            }
            if (shift && isLastLine(edit))
                return extendFromEdit(edit, 1)
            return false
        case Qt.Key_Escape:
            stopEditing(true)
            // A touch UI with a keyboard: the selected block has the selection's tools.
            if (touch && controller.selectionCount > 0)
                selectionMode = true
            return true
        }
        return false
    }

    /// Shift+↑ on the first line (`step` -1) / Shift+↓ on the last one (1) of the edited block: the
    /// text selection first goes to the block's start / end; once it is there, editing stops and
    /// the block and the previous / next one are selected (the block alone at the page's edge).
    function extendFromEdit(edit, step) {
        if (step < 0 && edit.cursorPosition > 0) {
            edit.moveCursorSelection(0, TextEdit.SelectCharacters)
            return true
        }
        if (step > 0 && edit.cursorPosition < edit.length) {
            edit.moveCursorSelection(edit.length, TextEdit.SelectCharacters)
            return true
        }
        const row = edit.row
        closeSuggestions()
        // From the edited block (the controller stops editing); at the day's edge into the next /
        // previous day of the journal stream.
        if (!controller.extendSelection(step) && !(stream !== null && stream.extendBy(controller, step)))
            controller.selectRange(row, row)
        if (inSpan)
            return true // (the day the selection's end went to takes over: StreamSelection.focusMoved)
        list.forceActiveFocus()
        if (touch && controller.selectionCount > 0)
            selectionMode = true
        ensureRowVisible(controller.selectedRow)
        return true
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
    /// subtitle, BToolTip; BBreadcrumb, BMenuItem and BToast show plain text): such text is rendered as styled text — images loaded, tags
    /// applied — when it looks like markup. Note text containing "<" is escaped, which AutoText
    /// then shows exactly as written.
    function autoTextSafe(s) {
        return s && s.indexOf("<") >= 0 ? TextHelper.escapeHtml(s) : (s || "")
    }

    /// Keys while blocks are selected (not editing): one block (Esc) or a range — Shift+↑/↓ extend
    /// or shrink it, Ctrl+A selects every block, Ctrl+C / Ctrl+X / Ctrl+V copy, cut and paste over
    /// it, Delete removes it, Tab / Shift+Tab, Alt+↑/↓ and Ctrl+Enter act on all of it.
    function handleSelectionKey(event) {
        if (!controller)
            return false
        const row = controller.selectedRow
        const ctrl = (event.modifiers & Qt.ControlModifier) !== 0
        const shift = (event.modifiers & Qt.ShiftModifier) !== 0
        const alt = (event.modifiers & Qt.AltModifier) !== 0
        // Blocks from one end to another, or picked one by one (a block selected alone — with the
        // children it brings along — keeps the block's own keys: Ctrl+Enter changes it, not its
        // children).
        const range = row >= 0 && ((controller.selectionAnchorRow >= 0 && controller.selectionAnchorRow !== row)
                                   || controller.selectionPicked)
        // Blocks of several days: copying only (Ctrl+X, Ctrl+V, Delete say so in their functions).
        if (inSpan && ((ctrl && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter))
                       || (!ctrl && (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab))
                       || (alt && (event.key === Qt.Key_Up || event.key === Qt.Key_Down)))) {
            spanRefuses()
            return true
        }
        if (ctrl) {
            switch (event.key) {
            case Qt.Key_Z: undoStep(shift); return true
            case Qt.Key_Y: undoStep(true); return true
            case Qt.Key_E: toggleSourceMode(); return true
            case Qt.Key_Period: if (row >= 0) controller.toggleFold(row); return true
            case Qt.Key_Return:
            case Qt.Key_Enter:
                if (row >= 0 && shift)
                    openTaskStatusMenuFor(row, range)
                else if (row >= 0 && range)
                    controller.cycleSelectionTasks()
                else if (row >= 0)
                    controller.cycleTask(row)
                return true
            case Qt.Key_C:
                if (shift) {
                    copyCurrentLink("blockRef") // nothing selected: says what to do
                    return true
                }
                if (row < 0)
                    return false
                copySelection(false)
                return true
            case Qt.Key_Insert:
                if (row < 0 || shift)
                    return false
                copySelection(false)
                return true
            case Qt.Key_X:
                if (row < 0 || shift)
                    return false
                cutSelection()
                return true
            case Qt.Key_V:
                if (row < 0 || shift)
                    return false
                pasteOverSelection()
                return true
            case Qt.Key_A:
                if (shift)
                    return false
                selectAllBlocks()
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
        if (shift && !alt) {
            switch (event.key) {
            case Qt.Key_Up:
            case Qt.Key_Down: {
                // Past the day's edge (or over several days already): the journal stream's.
                const step = event.key === Qt.Key_Down ? 1 : -1
                if (inSpan) {
                    stream.extendBy(controller, step)
                    return true
                }
                if (!controller.extendSelection(step) && stream !== null && stream.extendBy(controller, step))
                    return true
                // A touch UI with a keyboard: the range has the selection's tools and grips.
                if (touch && controller.selectionCount > 0)
                    selectionMode = true
                ensureRowVisible(controller.selectedRow)
                return true
            }
            case Qt.Key_Insert: pasteOverSelection(); return true
            case Qt.Key_Delete: cutSelection(); return true
            }
        }
        if (alt) {
            if (event.key === Qt.Key_Up) {
                if (!range) controller.moveUp(row); else if (selectionMovable()) controller.moveSelectionUp()
                ensureRowVisible(controller.selectedRow)
                return true
            }
            if (event.key === Qt.Key_Down) {
                if (!range) controller.moveDown(row); else if (selectionMovable()) controller.moveSelectionDown()
                ensureRowVisible(controller.selectedRow)
                return true
            }
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
            selectionMode = false
            if (controller.blocks.get(row).view === "table") {
                const item = list.itemAtIndex(row) as BlockDelegate
                if (item)
                    item.focusContent()
                return true
            }
            startEditing(row, -1)
            return true
        case Qt.Key_Delete:
        case Qt.Key_Backspace:
            deleteSelection()
            return true
        case Qt.Key_Tab:
            if (!range) controller.indent(row); else if (selectionMovable()) controller.indentSelection()
            return true
        case Qt.Key_Backtab:
            if (!range) controller.outdent(row); else if (selectionMovable()) controller.outdentSelection()
            return true
        case Qt.Key_Escape:
            leaveSelectionMode()
            return true
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
            if (editor.controller.editingRow < 0) {
                editor.closeSuggestions()
            } else {
                editor.selectionMode = false
                editor.ensureRowVisible(editor.controller.editingRow)
            }
        }
        function onSelectionChanged() {
            if (editor.controller.selectionCount === 0)
                editor.selectionMode = false
        }
        // A zoom ended or began while a block is edited (Enter in a zoomed heading or paragraph,
        // Undo / Redo across it, the breadcrumb): the rows around it come and go, the edited block
        // stays in view.
        // The row is read once the caret has moved (the zoom changes before it does). Undo / redo
        // signal the zoom every time: only a zoom that changed moves the view.
        function onZoomChanged() {
            const zoom = editor.controller.zoomBlock
            if (zoom === editor.shownZoom)
                return
            editor.shownZoom = zoom
            if (editor.controller.editingRow < 0)
                return
            // The breadcrumb coming or going changes the header's height: the view must not go
            // back to the page's top, away from the block (as revealRow).
            list.userMoved = true
            editor.later(() => {
                const row = editor.controller ? editor.controller.editingRow : -1
                if (row < 0 || row >= list.count)
                    return
                // The rows the zoom showed or hid get their heights first: positioned before, the
                // view kept its place among the zoom's rows and then showed the page's top.
                list.forceLayout()
                list.positionViewAtIndex(row, ListView.Contain)
                list.forceLayout()
                editor.ensureEditVisible()
            })
        }
        function onLoadingChanged() {
            if (!editor.controller.loading) {
                list.userMoved = false
                list.showsTop = true
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
        function onWebPicturesDownloaded(stored, failed) {
            if (failed.length > 0)
                editor.notice(qsTr("Could not save to the notebook: %1").arg(failed.join(", ")), "error")
            else if (stored > 0)
                editor.notice(qsTr("%n picture(s) saved to the notebook", "", stored), "success")
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
            showsTop = contentY <= originY - topMargin + 1
        }
        // autoHeight: every row is in the range the list fills at once (synchronously, or with
        // the incubation of the item holding the editor) — none waits for the asynchronous cache.
        displayMarginEnd: editor.autoHeight ? 1000000 : 0
        boundsBehavior: Flickable.StopAtBounds
        keyNavigationEnabled: false
        // No current item: a ListView gives its current item the focus whenever that item changes
        // (the first rows after a load, rows replaced by another program's change merged in) —
        // the block being edited lost the keyboard to a delegate and the next keys went nowhere.
        currentIndex: -1
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
        // A mouse's left drag selects blocks (the drag tracker below), it does not scroll: the
        // wheel and the touchpad do. Touch UIs: a finger flicks (and a mouse there too).
        acceptedButtons: editor.touch ? Qt.LeftButton : Qt.NoButton
        Accessible.role: Accessible.List
        Accessible.name: editor.controller ? editor.controller.title : ""

        /// The user scrolled: header size changes no longer move the view to the top.
        property bool userMoved: false
        onMovementStarted: userMoved = true
        /// The view shows the page's top (the header), as it does after loading. Kept as the view
        /// moves: the header growing upwards moves no contentY, so it still says where the view
        /// was when the header's height changes.
        property bool showsTop: true

        header: Column {
            width: list.width
            // The header (page title) gets its final height after the first layout: the top stays
            // in view. Only while the view shows it: scrolled further without a drag or the wheel
            // (the caret moved down the page with the keys, a selection dragged to the view's edge),
            // a title wrapping again in a narrower window moved the view back to the top, away from
            // the block being read or edited.
            onHeightChanged: if (!list.userMoved && list.showsTop) list.positionViewAtBeginning()
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
                    if (!editor.controller || editor.selectionMode)
                        return
                    const row = editor.controller.appendEmptyBlock()
                    editor.startEditing(row, -1)
                }
            }
            HoverHandler { cursorShape: Qt.IBeamCursor }
        }

        Keys.onPressed: (event) => {
            // Android's Back leaves the selection mode (before the shell goes back).
            if (event.key === Qt.Key_Back) {
                event.accepted = editor.selectionMode || editor.spanTools
                return
            }
            if (editor.forwardToEditingRow(event) || editor.handleSelectionKey(event))
                event.accepted = true
        }
        Keys.onReleased: (event) => {
            if (event.key === Qt.Key_Back && (editor.selectionMode || editor.spanTools)) {
                editor.leaveSelectionMode()
                event.accepted = true
            }
        }
        // The selection's keys win over application-wide shortcuts while the list has the keyboard.
        Keys.onShortcutOverride: (event) => {
            if (editor.controller && editor.controller.selectedRow >= 0 && (event.modifiers & Qt.ControlModifier)
                    && [Qt.Key_A, Qt.Key_C, Qt.Key_X, Qt.Key_V, Qt.Key_Z, Qt.Key_Y, Qt.Key_Return, Qt.Key_Enter].indexOf(event.key) >= 0)
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
                editor.openBlockMenuAt(editor.rowAt(p.x, p.y), scene)
            }
        }

        // The header (page title) gets its height after the first layout: start at the top.
        // (autoHeight: no scroll bar, the list never scrolls.)
        Component.onCompleted: {
            if (!editor.autoHeight)
                editor.scrollBar = scrollBarComponent.createObject(list, { list: list }) as T.ScrollBar
            editor.later(() => list.positionViewAtBeginning())
        }
        // Rows from one line to a table or a picture: the bar counts rows, its thumb does not jump.
        Component {
            id: scrollBarComponent
            BRowScrollBar {}
        }
    }

    // ---- Selecting blocks by dragging -------------------------------------------------------------
    /// The list's scroll bar (a press on it selects nothing).
    property T.ScrollBar scrollBar: null
    /// A press that started on block `startRow` and moved into another block selects the range
    /// (a mouse, a touchpad, a stylus; a finger only after a long press entered the selection mode,
    /// or on a grip). The state lives here, not in the delegates: a delegate may be destroyed while
    /// the list scrolls under the pointer. Near the view's top or bottom edge the list scrolls (the
    /// journal stream for a day: `scroller`).
    QtObject {
        id: drag
        property int startRow: -1
        /// Blocks are being selected by this press.
        property bool active: false
        /// A finger may select: the long press entered the selection mode, or a grip is held.
        property bool armed: false
        /// This press selected blocks: its release is no tap (blockTapped()).
        property bool selected: false
        property bool mouse: true
        /// The end that stays where it is (the anchor).
        property int fixedRow: -1
        property point scene: Qt.point(0, 0)
        /// Autoscroll, px per frame (negative: up).
        property real speed: 0
        /// The list's scrolling was held for this press (setScrollingHeld()).
        property bool holding: false
    }
    /// Blocks are being selected by dragging (tests read it).
    readonly property bool dragSelecting: drag.active

    function dragPressed(point) {
        drag.selected = false
        drag.active = false
        drag.armed = false
        drag.speed = 0
        drag.fixedRow = -1
        drag.scene = point.scenePosition
        drag.mouse = point.device === null || point.device.type !== PointerDevice.TouchScreen
        drag.startRow = -1
        if (!controller || controller.sourceMode)
            return
        if (scrollBar && scrollBar.visible && scrollBar.contains(scrollBar.mapFromItem(null, drag.scene.x, drag.scene.y)))
            return
        const p = list.contentItem.mapFromItem(null, drag.scene.x, drag.scene.y)
        drag.startRow = rowAt(p.x, p.y)
    }
    function dragMoved(point) {
        drag.scene = point.scenePosition
        if (!controller)
            return
        if (!drag.active) {
            if (drag.startRow < 0 || (!drag.mouse && !drag.armed))
                return
            const target = dragTarget()
            if (target.row < 0 || (target.editor === editor && target.row === drag.startRow)
                    || controller.blocks.get(drag.startRow).view === "frontmatter")
                return
            drag.active = true
            drag.selected = true
            if (drag.fixedRow < 0)
                drag.fixedRow = drag.startRow
            leaveEditingForSelection()
            setScrollingHeld(true)
        }
        updateDragSelection()
        updateAutoScroll()
    }
    function dragReleased() {
        const selecting = drag.active || drag.armed
        drag.active = false
        drag.armed = false
        drag.speed = 0
        drag.fixedRow = -1
        drag.startRow = -1
        setScrollingHeld(false)
        if (selecting && controller && controller.selectionCount > 0) {
            list.forceActiveFocus()
            if (touch)
                selectionMode = true
        }
    }
    /// The selection from the fixed end to the block under the pointer (in another day of the
    /// journal stream: a selection over several days).
    function updateDragSelection() {
        if (!drag.active || !controller || drag.fixedRow < 0)
            return
        const target = dragTarget()
        if (target.row < 0 || target.editor.controller.blocks.get(target.row).view === "frontmatter")
            return
        if (target.editor !== editor)
            stream.extendFrom(controller, drag.fixedRow, target.editor.controller, target.row)
        else
            controller.selectRange(drag.fixedRow, target.row) // (back in this day: a span ends)
    }
    /// The block under the pointer: { editor, row } — this editor's nearest block, or, below or
    /// above this day of the journal stream, the block of the day there.
    function dragTarget() {
        const local = { editor: editor, row: rowNear(drag.scene) }
        if (stream === null || !scroller || !scroller.dayAt)
            return local
        const p = editor.mapFromItem(null, drag.scene.x, drag.scene.y)
        if (p.y >= 0 && p.y < editor.height)
            return local
        const day = scroller.dayAt(drag.scene)
        const other = day ? day.editor : null
        if (!other || other === editor || !other.controller || !other.controller.loaded)
            return local
        const row = other.rowNear(drag.scene)
        return row >= 0 ? { editor: other, row: row } : local
    }
    /// The block at a scene point, the nearest one when the point is above or below the blocks (the
    /// header, the footer, outside a day of the journal stream) or outside the view.
    function rowNear(scene) {
        if (!controller)
            return -1
        const p = list.contentItem.mapFromItem(null, scene.x, scene.y)
        const y = Math.max(list.contentY + 1, Math.min(list.contentY + list.height - 1, p.y))
        let row = rowAt(contentX + 1, y)
        if (row >= 0)
            return row
        const headerBottom = list.headerItem ? list.headerItem.y + list.headerItem.height : list.originY
        if (y <= headerBottom) {
            row = visualNext(-1)
            while (row >= 0 && controller.blocks.get(row).view === "frontmatter")
                row = controller.nextVisibleRow(row)
            return row
        }
        return controller.lastVisibleRow()
    }
    /// Scroll near the view's top and bottom edges while selecting (the toolbar's top for the bottom).
    function updateAutoScroll() {
        if (!drag.active) {
            drag.speed = 0
            return
        }
        const host = dockHost
        const y = host.mapFromItem(null, drag.scene.x, drag.scene.y).y
        const top = host === editor ? list.y : 0
        const bottom = host.height - (toolbarVisible ? toolbarHeight : 0)
        const zone = Math.max(16, Math.min(56, (bottom - top) / 5))
        if (y < top + zone)
            drag.speed = -Math.min(40, (top + zone - y) / 2 + 2)
        else if (y > bottom - zone)
            drag.speed = Math.min(40, (y - (bottom - zone)) / 2 + 2)
        else
            drag.speed = 0
        // A day of the journal stream: the stream scrolls while the day goes on beyond the edge —
        // and, selecting into the days around it (`stream`), while it is still near enough to stay
        // in the list's cache (scrolled further, it would be destroyed and the drag with it).
        if (scroller && host !== editor) {
            const dayTop = editor.mapToItem(host, 0, 0).y
            const reach = stream !== null && scroller.selectionReach !== undefined ? scroller.selectionReach : 0
            const past = reach > 0 ? top - reach : bottom
            const before = reach > 0 ? bottom + reach : top
            if ((drag.speed > 0 && dayTop + editor.height <= past) || (drag.speed < 0 && dayTop >= before))
                drag.speed = 0
        }
    }
    /// Scroll the editor (or the journal stream it is part of) by `dy`, within its content.
    function scrollBy(dy) {
        if (scroller) {
            scroller.scrollBy(dy)
            return
        }
        const min = list.originY - list.topMargin
        const max = Math.max(min, list.contentHeight + list.originY - list.height + list.bottomMargin)
        list.contentY = Math.max(min, Math.min(max, list.contentY + dy))
    }
    /// A finger selecting must not flick the list (or the journal stream) meanwhile.
    function setScrollingHeld(held) {
        if (drag.holding === held || (held && drag.mouse))
            return
        drag.holding = held
        if (scroller && scroller.holdScrolling)
            scroller.holdScrolling(held)
        else if (!autoHeight)
            list.interactive = !held
    }
    /// A long press on the text of `row` (touch UIs; not the block being edited): the selection
    /// mode with that block selected — or, already in it, the selection extended there; moving the
    /// finger without lifting it extends it further.
    function longPressText(row) {
        if (!touch || !controller || row < 0 || controller.blocks.get(row).view === "frontmatter")
            return
        if (selectionMode && pickMode && controller.selectionCount > 0) {
            // "Pojedynczo": a long press picks like a tap (its release is no tap).
            toggleBlock(row)
            drag.selected = true
            return
        }
        if (selectionMode && controller.selectionCount > 0) {
            controller.extendSelectionTo(row)
        } else {
            selectBlock(row)
        }
        drag.armed = true
        drag.selected = true
        drag.mouse = false
        drag.startRow = row
        drag.fixedRow = controller.selectionAnchorRow >= 0 ? controller.selectionAnchorRow : row
        setScrollingHeld(true)
    }
    /// A grip at the band's start (`atStart`) or end is pressed: the other end stays, the finger
    /// moves this one (the drag goes on when the grip's block scrolls away and is destroyed).
    function beginGripDrag(atStart) {
        if (!controller || controller.selectionCount === 0)
            return
        drag.fixedRow = atStart ? controller.selectionLastRow : controller.selectionFirstRow
        drag.armed = true
        drag.active = true
        drag.selected = true
        drag.mouse = false
        setScrollingHeld(true)
    }

    Item {
        id: dragLayer
        anchors.fill: list
        z: 2
        PointHandler {
            id: dragPointer
            target: null
            enabled: editor.controller !== null && !editor.controller.sourceMode
            onActiveChanged: active ? editor.dragPressed(point) : editor.dragReleased()
            onPointChanged: if (active) editor.dragMoved(point)
        }
    }
    Timer {
        id: autoScroll
        interval: 16
        repeat: true
        running: drag.active && drag.speed !== 0
        onTriggered: {
            editor.scrollBy(drag.speed)
            editor.updateDragSelection()
            editor.updateAutoScroll()
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
            const row = editor.rowAt(editor.contentX + 1, p.y)
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
        menuAbove = null
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
    /// The block menu of a right-click at `scene` (scene coordinates) on `row`: the list's handler,
    /// the rendered text's when the click was on no date chip. None for the page properties.
    function openBlockMenuAt(row, scene) {
        if (!controller || row < 0 || row >= controller.blocks.count || controller.blocks.get(row).view === "frontmatter")
            return
        const at = list.mapFromItem(null, scene.x, scene.y)
        openBlockMenu(row, list, at.x, at.y)
    }
    /// The touch toolbar's "Więcej": the menu of the block being edited, right above the button.
    /// The keyboard goes (the menu takes the keys, which hides it on Android anyway): the menu
    /// follows the toolbar down into the room it leaves (followToolbarWith), every item shown.
    function openBlockMenuAbove(row, item) {
        if (!controller || row < 0)
            return
        if (!blockMenu)
            blockMenu = blockMenuComponent.createObject(editor) as BlockMenu
        blockMenu.openFor(row)
        const y = -blockMenu.openHeight() - Theme.space1
        openBlockMenu(row, item, 0, y)
        followToolbarWith(blockMenu, item, y)
        Qt.inputMethod.hide()
    }
    /// The cell toolbar's "Więcej": the block menu of the table whose cell is being edited. A cell
    /// is no block editor that could go on under the menu: its editing ends (its text kept, the
    /// keyboard hidden), the table is selected — the block the menu acts on, the current block of
    /// the palette's commands afterwards — and the menu opens at the table.
    function openTableBlockMenu(table) {
        if (!controller || !table || !table.block)
            return
        const row = table.block.index
        if (table.cellInput)
            EditorInput.commitPreedit(table.cellInput)
        Qt.inputMethod.hide()
        table.closeCell()
        controller.selectedRow = row
        list.forceActiveFocus()
        list.forceLayout()
        const item = list.itemAtIndex(row) as BlockDelegate
        openBlockMenu(row, item, item ? item.contentLeft : 0, 0)
    }
    Component {
        id: blockMenuComponent
        BlockMenu {
            editor: editor
        }
    }

    // ---- Pictures from the internet ------------------------------------------------------------
    /// Store the picture from the internet `url` — "" every one on the page — in the notebook
    /// (PageController.downloadWebPictures; the toast comes with webPicturesDownloaded).
    function downloadWebPictures(url) {
        if (!controller)
            return
        const count = controller.downloadWebPictures(url || "")
        if (count < 0)
            notice(qsTr("Switch back to blocks (Ctrl+E) to save the pictures in the notebook"), "info")
        else if (count === 0 && !url)
            notice(qsTr("This page has no pictures from the internet"), "info")
    }
    /// WebPictureMenu for a shown picture from the internet (EmbedView: a right-click or long
    /// press on it; "⋯" in its viewer). Created when first opened.
    property WebPictureMenu webPictureMenu: null
    property string webPictureMenuUrl: ""
    property int webPictureMenuRow: -1
    function openWebPictureMenu(row, url, host, item, x, y) {
        if (!controller)
            return
        if (!webPictureMenu)
            webPictureMenu = webPictureMenuComponent.createObject(editor) as WebPictureMenu
        menuAbove = null
        closeSuggestions()
        webPictureMenuUrl = url
        webPictureMenuRow = row
        webPictureMenu.host = host
        webPictureMenu.canView = !(webPictureViewer && webPictureViewer.visible)
        webPictureMenu.bottomMargin = menuBottomMargin()
        webPictureMenu.popup(item, x, y)
    }
    /// WebPictureViewer: a shown picture from the internet on its own (EmbedView: a click on it;
    /// "Powiększ" in its menu). `picture` is its embed. Created when first opened.
    property WebPictureViewer webPictureViewer: null
    property int webPictureViewerRow: -1
    function openWebPictureViewer(row, picture) {
        if (!controller || !picture || picture.state !== "ready")
            return
        if (!webPictureViewer)
            webPictureViewer = webPictureViewerComponent.createObject(editor) as WebPictureViewer
        menuAbove = null
        closeSuggestions()
        webPictureViewerRow = row
        webPictureViewer.picture = picture
        webPictureViewer.open()
    }
    /// The embed of the picture from the internet `url` in row `row` ({} when not shown there).
    function webPictureEmbed(row, url) {
        const item = list.itemAtIndex(row) as BlockDelegate
        const embeds = item && item.embeds ? item.embeds : []
        for (let i = 0; i < embeds.length; ++i) {
            if (embeds[i].web && embeds[i].target === url)
                return embeds[i]
        }
        return ({})
    }
    function chooseWebPictureAction(action) {
        const url = webPictureMenuUrl
        if (!controller || url === "")
            return
        switch (action) {
        case "view": openWebPictureViewer(webPictureMenuRow, webPictureEmbed(webPictureMenuRow, url)); break
        case "download":
            downloadWebPictures(url)
            if (webPictureViewer)
                webPictureViewer.close()
            break
        case "open": controller.activateLink(webPictureMenuRow, "ext:" + url, false); break
        case "copy":
            if (controller.copyWebPictureUrl(url))
                notice(qsTr("Picture address copied"), "success")
            else
                notice(qsTr("Could not copy: the clipboard is not available"), "error")
            break
        }
    }
    Component {
        id: webPictureMenuComponent
        WebPictureMenu {
            onChosen: (action) => editor.chooseWebPictureAction(action)
        }
    }
    Component {
        id: webPictureViewerComponent
        WebPictureViewer {
            id: pictureViewer
            onSaveRequested: {
                const url = pictureViewer.picture ? pictureViewer.picture.target : ""
                pictureViewer.close()
                if (url)
                    editor.downloadWebPictures(url)
            }
            onMenuRequested: (item) => {
                const p = pictureViewer.picture || {}
                editor.openWebPictureMenu(editor.webPictureViewerRow, p.target || "", p.host || "", item, 0, item.height)
            }
        }
    }

    // ---- Token menus: dates, reminders, 🔁, priorities, days (DateChipMenu) ---------------------
    /// DateChipMenu for a token of rendered text (`link`: its date:, reminder:, recur:, prio: or day:
    /// link): a right-click or long press on it — in a block (BlockText: openDateMenu()), a query
    /// result or an embed of another block (openTokenMenuIn()), a table cell (openCellTokenMenu()).
    /// Created when first opened; the menu's choice goes to the `apply(action, a, b)` its opener
    /// gave (popupTokenMenu()). A block is tracked while its menu is open and the chip's range is
    /// used only while the block is unchanged (PageController.trackedRow).
    property DateChipMenu dateMenu: null
    property int dateMenuBlock: -1
    property var dateMenuChip: ({})
    property var dateMenuApply: null
    function isDateChipLink(link) {
        return link.startsWith("date:") || link.startsWith("reminder:") || link.startsWith("recur:")
                || link.startsWith("prio:") || link.startsWith("day:")
    }
    /// Links whose click edits the text (their menu is the right button's): date, 🔁 and priority chips.
    function editsOnClick(link) {
        return link.startsWith("date:") || link.startsWith("recur:") || link.startsWith("prio:")
    }
    /// Open DateChipMenu for `chip` (PageController.dateChip / tokenChipIn / tableCellChip) at
    /// (x, y) of `item`; `apply(action, a, b)` does what is chosen (DateChipMenu.chosen).
    function popupTokenMenu(chip, apply, item, x, y) {
        if (!dateMenu)
            dateMenu = dateMenuComponent.createObject(editor) as DateChipMenu
        menuAbove = null
        closeSuggestions()
        dateMenuChip = chip
        dateMenuApply = apply
        dateMenu.kind = chip.kind
        dateMenu.alarm = chip.alarm === true
        dateMenu.rule = chip.rule || ""
        dateMenu.priority = chip.priority || ""
        dateMenu.logseq = chip.logseq === true
        dateMenu.isTask = chip.isTask === true
        dateMenu.occurrence = false
        // Above the docked toolbar and the keyboard (it flips above its item when needed).
        dateMenu.bottomMargin = menuBottomMargin()
        dateMenu.popup(item, x, y)
    }
    function chooseDateAction(action, a, b) {
        const apply = dateMenuApply
        dateMenuApply = null
        if (apply)
            apply(action, a, b)
    }
    /// The menu of a token in block `row`'s text. False when `link` names none.
    function openDateMenu(row, link, item, x, y) {
        if (!controller || row < 0 || row >= controller.blocks.count || !isDateChipLink(link))
            return false
        const chip = controller.dateChip(row, link)
        if (!chip.kind)
            return false
        if (dateMenuBlock >= 0)
            controller.untrack(dateMenuBlock)
        dateMenuBlock = controller.trackBlock(row)
        const handle = dateMenuBlock
        popupTokenMenu(chip, (action, a, b) => editor.chooseOwnToken(handle, chip, action, a, b), item, x, y)
        return true
    }
    function chooseOwnToken(handle, chip, action, a, b) {
        if (!controller)
            return
        const row = controller.trackedRow(handle)
        controller.untrack(handle)
        if (dateMenuBlock === handle)
            dateMenuBlock = -1
        if (row < 0)
            return
        switch (action) {
        case "change":
            if (chip.kind === "reminder")
                dialogs.editReminder(row, chip.from, chip.to)
            else if (chip.kind === "recurrence")
                dialogs.pickRecurrenceAt(row, chip.from, chip.to, chip.rule)
            else
                dialogs.pickDateAt(row, chip.from, chip.to, chip.kind, chip.at)
            break
        case "blockRecurrenceChange": dialogs.pickRecurrence(row); break
        case "open": activateLink(row, chip.url); break
        case "openSide": controller.activateLink(row, chip.url, true); break
        case "copy": copyText(chip.copyText); break
        default: controller.applyTokenAction(row, chip.from, chip.to, action, a, b); break
        }
    }
    /// The menu of a token in a copy of a block of `path` (a query result, an embed): `raw` is the
    /// block's Markdown as the index has it, `line` its line. The edit goes to that block's file
    /// (PageController.editTokenIn) with a toast to undo it; on this page it is an undo step.
    function openTokenMenuIn(path, line, raw, link, item, x, y) {
        if (!controller || !path || line < 0 || !raw || !isDateChipLink(link))
            return false
        const chip = controller.tokenChipIn(raw, link)
        if (!chip.kind)
            return false
        const run = (action, a, b) => editor.reportTokenEdit(controller.editTokenIn(path, line, raw, chip.ref, action, a, b), action)
        popupTokenMenu(chip, (action, a, b) => editor.chooseTokenWith(chip, run, action, a, b), item, x, y)
        return true
    }
    /// The menu of a token in a table cell (tableRow -1 = the header): the cell's text changes
    /// (PageController.applyTableCellToken, one undo step).
    function openCellTokenMenu(row, tableRow, column, link, item, x, y) {
        if (!controller || row < 0 || !isDateChipLink(link))
            return false
        const chip = controller.tableCellChip(row, tableRow, column, link)
        if (!chip.kind)
            return false
        if (dateMenuBlock >= 0)
            controller.untrack(dateMenuBlock)
        dateMenuBlock = controller.trackBlock(row)
        const handle = dateMenuBlock
        const run = (action, a, b) => {
            const r = controller.trackedRow(handle)
            if (r >= 0)
                controller.applyTableCellToken(r, tableRow, column, chip.from, chip.to, action, a, b)
        }
        popupTokenMenu(chip, (action, a, b) => editor.chooseTokenWith(chip, run, action, a, b), item, x, y)
        return true
    }
    /// A token menu's choice made with `run(action, a, b)`; "change" opens the dialog that ends in it.
    function chooseTokenWith(chip, run, action, a, b) {
        switch (action) {
        case "change":
            if (chip.kind === "reminder")
                dialogs.editReminderWith(run, chip)
            else if (chip.kind === "recurrence")
                dialogs.pickRecurrenceWith((act, rule) => run(act, rule, 0), chip.rule, "recurrence")
            else
                dialogs.pickDateWith(run, chip.kind, chip.at)
            break
        case "blockRecurrenceChange":
            dialogs.pickRecurrenceWith((act, rule) => run(act, rule, 0), chip.rule, "blockRecurrence")
            break
        default: run(action, a, b); break
        }
    }
    /// The toast after editTokenIn(): what changed, with "Undo" for another file; why not.
    function reportTokenEdit(r, action) {
        if (!r)
            return
        if (r.ok) {
            if (r.undo > 0) {
                const undo = r.undo
                const text = action === "recurrence" || action === "blockRecurrence" ? qsTr("Repeat changed")
                           : action === "priority" || action === "blockPriority" ? qsTr("Priority changed")
                           : action === "remove" ? qsTr("Removed")
                           : action === "alarm" || action === "reminder" ? qsTr("Reminder changed")
                           : qsTr("Date changed")
                notice(text, "success", {
                    text: qsTr("Undo"),
                    run: () => {
                        if (!editor.controller.undoTokenEdit(undo))
                            editor.notice(qsTr("The page changed in the meantime; nothing was undone."), "info")
                    }
                })
            }
            return
        }
        if (r.reason === "gone" || r.reason === "changed")
            notice(qsTr("The note changed in the meantime; nothing was changed."), "info")
        else if (r.reason === "failed")
            notice(qsTr("The change could not be saved."), "error")
    }
    Component {
        id: dateMenuComponent
        DateChipMenu {
            onChosen: (action, a, b) => editor.chooseDateAction(action, a, b)
        }
    }

    // ---- The menu of a link: [[page]], #tag, ((block)), web address (LinkMenu) ------------------
    /// LinkMenu for a page:, tag:, block: or ext: link of rendered text — `fromPath`: the page the
    /// link is written on ("" = this one) —; `open(newPane)` does what a click does (in the side
    /// panel with newPane). Created when first opened. False when `link` is no such link.
    property LinkMenu linkMenu: null
    property var linkMenuInfo: ({})
    property var linkMenuOpen: null
    function isLinkMenuLink(link) {
        return link.startsWith("page:") || link.startsWith("tag:") || link.startsWith("block:") || link.startsWith("ext:")
    }
    function openLinkMenu(link, fromPath, open, item, x, y) {
        if (!controller || !isLinkMenuLink(link))
            return false
        const info = controller.linkMenu(link, fromPath || "")
        if (!info.kind)
            return false
        if (!linkMenu)
            linkMenu = linkMenuComponent.createObject(editor) as LinkMenu
        menuAbove = null
        closeSuggestions()
        linkMenuInfo = info
        linkMenuOpen = open
        linkMenu.kind = info.kind
        linkMenu.unresolved = info.unresolved === true
        linkMenu.bottomMargin = menuBottomMargin()
        linkMenu.popup(item, x, y)
        return true
    }
    /// The link menu of a link in block `row`'s text.
    function openBlockLinkMenu(row, link, item, x, y) {
        return openLinkMenu(link, "", (newPane) => {
            if (newPane)
                editor.controller.activateLink(row, link, true)
            else
                editor.activateLink(row, link)
        }, item, x, y)
    }
    function chooseLinkAction(action) {
        const info = linkMenuInfo
        const open = linkMenuOpen
        linkMenuOpen = null
        if (!controller)
            return
        switch (action) {
        case "open": if (open) open(false); break
        case "openSide": if (open) open(true); break
        case "copy": copyText(info.copyText); break
        case "overview": controller.navigate({ view: "tag", tag: info.tag, overview: true }); break
        case "tasks": if (linkMenu) controller.navigate({ view: "tasks", query: linkMenu.tasksQuery(info.tag) }); break
        }
    }
    /// Copy `text` and say so ("Skopiowano …", or that the clipboard is not available).
    function copyText(text) {
        if (!controller || !text)
            return false
        const r = controller.copyText(text)
        if (r.ok)
            notice(qsTr("Copied %1").arg(r.text), "success")
        else
            notice(copyFailure(r.reason), "error")
        return r.ok
    }
    Component {
        id: linkMenuComponent
        LinkMenu {
            onChosen: (action) => editor.chooseLinkAction(action)
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
    /// The menu sets the status of every block from one end of the selection to the other
    /// (Ctrl+Shift+Enter on a range: PageController.setSelectionTaskStatus), not of one block.
    property bool taskMenuForSelection: false
    function openTaskStatusMenu(row, item, x, y) {
        if (!controller || row < 0 || row >= controller.blocks.count)
            return
        taskMenuForSelection = false
        if (!taskMenu)
            taskMenu = taskMenuComponent.createObject(editor) as TaskStatusMenu
        menuAbove = null
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
    /// selected block's first line; `selection`: for every block of the selected range (`row` is
    /// its focus end).
    function openTaskStatusMenuFor(row, selection) {
        if (!canHaveTaskStatus(row) && selection !== true)
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
        taskMenuForSelection = selection === true
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
        const y = -taskMenu.openHeight() - Theme.space1
        openTaskStatusMenu(row, item, 0, y)
        followToolbarWith(taskMenu, item, y)
    }

    // ---- Menus above the docked toolbar ---------------------------------------------------------
    /// A menu opened right above the docked toolbar (its Więcej, its task button held down), the
    /// toolbar's button it is above and its place there. Such a menu takes the keys, so the system
    /// hides the on-screen keyboard and the toolbar drops to the bottom (it stays docked while the
    /// editing goes on): the menu follows it — and gets the room the keyboard leaves, every item
    /// shown, none scrolled away — until something in it is touched (BMenu.touched): it never
    /// moves away from a finger that aims at it.
    property BMenu menuAbove: null
    property Item menuAboveAnchor: null
    property real menuAboveY: 0
    function followToolbarWith(menu, anchor, y) {
        menuAbove = menu && menu.visible ? menu : null
        menuAboveAnchor = anchor
        menuAboveY = y
    }
    function followToolbar() {
        const menu = menuAbove
        const anchor = menuAboveAnchor
        if (!menu || !anchor || !menu.visible || menu.touched)
            return
        const margin = menuBottomMargin()
        if (Math.abs(margin - menu.bottomMargin) < 1)
            return
        menu.bottomMargin = margin
        menu.popup(anchor, 0, menuAboveY) // (open already: placed again)
    }
    Connections {
        target: KeyboardInsets
        enabled: editor.menuAbove !== null
        function onOverlapChanged() { Qt.callLater(editor.followToolbar) }
    }
    Connections {
        target: editor.dockHost
        enabled: editor.menuAbove !== null
        function onHeightChanged() { Qt.callLater(editor.followToolbar) }
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
        if (taskMenuForSelection) {
            taskMenuForSelection = false
            controller.setSelectionTaskStatus(status) // one undo step; what already has it stays
        } else if (row >= 0 && status !== taskStatusOf(row)) {
            controller.setTaskStatus(row, status)
        }
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

    // ---- The selection's tools on desktops ---------------------------------------------------------
    // A compact bar at the top of the view while two or more blocks are selected (touch UIs dock
    // the tools on the keyboard's place instead: the "selection" toolbarMode).
    Loader {
        id: floatingSelectionBar
        objectName: "floatingSelectionBar"
        parent: editor.dockHost
        active: !editor.touch && editor.controller !== null && !editor.controller.sourceMode
                && (editor.inSpan ? editor.spanTools : editor.controller.multiSelection)
        visible: active
        z: 11
        anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
        y: Theme.space2
        sourceComponent: Item {
            implicitWidth: card.width
            implicitHeight: card.height
            BShadow { target: card; level: 2 }
            Rectangle {
                id: card
                width: tools.implicitWidth + 2 * Theme.space1
                height: Theme.controlHeight + 2 * Theme.space1
                radius: Theme.radiusLg
                color: Theme.bgElevated
                border.width: 1
                border.color: Theme.border
                SelectionToolbar {
                    id: tools
                    objectName: "floatingSelectionTools"
                    x: Theme.space1
                    y: Theme.space1
                    height: Theme.controlHeight
                    editor: editor
                }
            }
        }
    }

    // ---- The edited text's menu (desktop right-click) -------------------------------------------------
    /// Wytnij, Kopiuj, Wklej, Zaznacz wszystko for the text of the block being edited: a right-click
    /// in it with a mouse, touchpad or stylus (a bare TextEdit has no menu of its own; touch UIs have
    /// the system's selection handles and popup). Made when first opened.
    property BMenu textMenu: null
    property InlineEditor textMenuEdit: null
    function openTextMenu(edit, x, y) {
        if (!edit || !controller)
            return
        if (!textMenu)
            textMenu = textMenuComponent.createObject(editor) as BMenu
        textMenuEdit = edit
        // A click outside the selected text moves the caret there first.
        const pos = edit.positionAt(x, y)
        if (pos < edit.selectionStart || pos > edit.selectionEnd || edit.selectionStart === edit.selectionEnd)
            edit.cursorPosition = pos
        textMenu.bottomMargin = menuBottomMargin()
        textMenu.popup(edit, x, y)
    }
    Component {
        id: textMenuComponent
        BMenu {
            id: menu
            objectName: "editTextMenu"
            readonly property InlineEditor edit: editor.textMenuEdit
            readonly property bool hasSelection: edit !== null && edit.selectionStart !== edit.selectionEnd
            BMenuItem {
                objectName: "editTextCut"
                text: qsTr("Cut")
                iconName: "scissors"
                shortcut: "Ctrl+X"
                enabled: menu.hasSelection
                onTriggered: editor.copyEditSelection(menu.edit, true)
            }
            BMenuItem {
                objectName: "editTextCopy"
                text: qsTr("Copy")
                iconName: "copy"
                shortcut: "Ctrl+C"
                enabled: menu.hasSelection
                onTriggered: editor.copyEditSelection(menu.edit, false)
            }
            BMenuItem {
                objectName: "editTextPaste"
                // Not in the browser: a page reads the clipboard only from a paste event (Ctrl+V).
                visible: DeviceInfo.platform !== "wasm"
                height: visible ? implicitHeight : 0
                text: qsTr("Paste")
                iconName: "clipboard-paste"
                shortcut: "Ctrl+V"
                onTriggered: {
                    if (menu.edit && !menu.edit.multiline && editor.controller.clipboardHasImage())
                        editor.controller.pasteImage(menu.edit.row, menu.edit.selectionStart, menu.edit.selectionEnd)
                    else if (menu.edit && (menu.edit.multiline || !editor.pasteIntoEdit(menu.edit)))
                        menu.edit.paste()
                }
            }
            BMenuSeparator {}
            BMenuItem {
                objectName: "editTextSelectAll"
                text: qsTr("Select all")
                shortcut: "Ctrl+A"
                onTriggered: if (menu.edit) menu.edit.selectAll()
            }
            BMenuItem {
                objectName: "editTextBlockMenu"
                text: qsTr("Block…")
                iconName: "ellipsis"
                onTriggered: {
                    const edit = menu.edit
                    if (edit)
                        editor.later(() => editor.openBlockMenu(edit.row, edit, 0, edit.cursorRectangle.y + edit.cursorRectangle.height))
                }
            }
            // The keyboard goes back to the edited text (its selection stayed: persistentSelection).
            onClosed: if (edit && edit.active) edit.ensureFocus()
        }
    }

    /// The dialogs (for tests and the playground's scripted states).
    readonly property alias dialogs: dialogs
    readonly property alias listView: list
}
