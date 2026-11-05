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
// a new block after the one under the drop), "/Obraz…" and the toolbar pick files ("/Plik…" links them instead).
// A leading "# Title" repeating the page title is shown above the page properties
// (PageController.titleRow, see BlockDelegate); Up/Down follow that on-screen order.
// `message(text, kind, action)` (emitted by notice()) tells what the host should show as a toast;
// `action` — an EditorToastAction (`text`, run()) or null — is its button (deleting or cutting
// blocks: "Cofnij").
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

    final property PageController controller
    /// Shown above the blocks (e.g. the page title); scrolls with them.
    final property Component header: null
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
    // A heading: clearly closer to what follows than to what precedes it, with air on both sides.
    readonly property int headingGapBelow: Math.round(fontSize * 0.6)
    function headingGapAbove(level: int): int {
        return Math.round(fontSize * (level === 1 ? 1.8 : level === 2 ? 1.5 : level === 3 ? 1.25 : 1.05))
    }
    readonly property real taskBoxWidth: Theme.touchUi ? 26 : 24
    readonly property real contentX: Math.max(horizontalPadding, (width - maxTextWidth) / 2)
    readonly property real contentWidth: Math.max(120, Math.min(maxTextWidth, width - 2 * horizontalPadding))
    /// Where the caret/selection lives: the inline editor (TextEdit) being typed in.
    final property InlineEditor activeEditor: null
    /// activeEditor = `input` (an InlineEditor, or null; see updateCompletion()).
    function setActiveEditor(input: TextEdit): void {
        activeEditor = input as InlineEditor
    }
    readonly property bool touch: Theme.touchUi

    // ---- The touch toolbar, docked on the keyboard (docs/UI.md "On-screen keyboard") ------------
    /// The table whose cell is being edited (TableBlockView sets and clears it), else null.
    final property TableBlockView editingTable: null
    /// The table whose cell is being edited (TableBlockView: itself, or null).
    function setEditingTable(table: Item): void {
        editingTable = table as TableBlockView
    }
    /// Source mode with its text being edited (SourceEditor: the text has the keyboard).
    property bool sourceEditing: false
    /// The tools of the touch toolbar: "block" (a block is being edited), "cell" (a table cell),
    /// "source" (source mode), "" (none: no toolbar). Stays while the system hides the keyboard
    /// and the editing goes on. One slot: another tool set docks the same way (a new mode).
    readonly property string toolbarMode: !touch || controller === null ? ""
                                          : controller.sourceMode ? (sourceEditing ? "source" : "")
                                          : controller.editingRow >= 0 ? (findKeepsKeyboard ? "" : "block")
                                          : editingTable !== null ? "cell"
                                          : (inSpan ? spanTools : selectionMode && selectionCount > 0) ? "selection" : ""
    readonly property bool toolbarVisible: toolbarMode !== ""
    readonly property real toolbarHeight: Theme.editToolbarHeight
    /// Where the toolbar docks: an item that does not scroll with the blocks and whose bottom
    /// edge the shell keeps on the keyboard — the editor itself (null) on a page, the journal
    /// stream's frame for its days (ContentJournalDay). The toolbar fills its bottom edge.
    final property Item toolbarHost: null
    readonly property Item dockHost: toolbarHost !== null ? toolbarHost : editor
    /// What scrolls the editor instead of its own list (a day of the journal stream: the journal
    /// list; any object with ensureVisible(item, rect), and centerInView(item, rect) for
    /// typewriter scrolling), else null.
    final property EditorScroller scroller: null
    /// Typewriter scrolling (AppSettings.typewriterScrolling; docs/UI.md "Statistics and focus
    /// mode"): the caret's line is kept in the middle of the view while writing (the view gives
    /// room under the last block for it: PageView's bottomSpace).
    property bool typewriter: false
    /// The editor's own keys for copying a block reference and for source mode, handled here only
    /// while they are those commands' shortcuts (the views bind the shell's ShortcutMap: another
    /// key is a window Shortcut that asks the view instead); the block menu shows the first.
    property string copyRefShortcut: "Ctrl+Shift+C"
    property string sourceModeShortcut: "Ctrl+E"
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

    /// Something to tell the user (a toast): kind "error" | "info" | "success"; `action` — an
    /// EditorToastAction (`text`, run()) or null — a button on the toast (after deleting or cutting
    /// blocks: "Cofnij", undoAction()).
    signal message(string text, string kind, EditorToastAction action)
    /// Say `text` (message()); `action` (null: none) is the toast's button (EditorToastAction:
    /// `text` and `run()`, which the host calls), e.g. undoAction()'s.
    function notice(text: string, kind: string, action: EditorToastAction): void {
        message(text, kind, action)
    }
    /// notice() with undoAction(before) (a plain toast when the edit changed nothing).
    function noticeUndoable(text: string, kind: string, before: string): void {
        notice(text, kind, undoAction(before))
    }
    /// The "Cofnij" of a toast about the edit just made (`before`: controller.undoToken() before
    /// it; null when it changed nothing): undoes that edit while it is still the last step of this
    /// page's history (PageController.undoStep) — after other edits, on another page or once undone
    /// it says so instead.
    function undoAction(before: string): EditorToastAction {
        const ctl = controller
        if (!ctl)
            return null
        const token = ctl.undoToken()
        if (token === before)
            return null
        const action = toastAction("undoStep", qsTr("Undo"))
        action.controller = ctl
        action.path = ctl.path
        action.token = token
        return action
    }
    /// A toast's action (EditorToastAction: `kind` and its values), the editor's child.
    /// The "Cofnij" of a replace over days of the journal stream (FindBar): StreamFind.undo(token).
    function streamUndoAction(stream: StreamFind, token: int): EditorToastAction {
        const action = toastAction("streamUndo", qsTr("Undo"))
        action.stream = stream
        action.undo = token
        return action
    }
    function toastAction(kind: string, text: string): EditorToastAction {
        return toastActionComponent.createObject(editor, { kind: kind, text: text }) as EditorToastAction
    }
    Component {
        id: toastActionComponent
        EditorToastAction {
            editorItem: editor
        }
    }
    /// `ctl` is still this editor's controller, on the page `path`.
    function samePage(ctl: PageController, path: string): bool {
        return controller === ctl && ctl !== null && ctl.path === path
    }
    /// The `row` of a PageController result map (-1 without one).
    function resultRow(r: var): int {
        rowResult.map = r
        return rowResult.map.row === undefined ? -1 : rowResult.map.row
    }
    MapHolder { id: rowResult }
    /// The number of `left` paths of StreamFind.undo()'s result.
    function leftCount(r: var): int {
        leftResult.map = r
        return leftResult.left().length
    }
    MapHolder {
        id: leftResult
        function left(): list<string> { return leftResult.map.left }
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
    final property StreamSelection stream: null
    /// Blocks of several days are selected (this day may hold a part of them, or none).
    readonly property bool inSpan: stream !== null && stream.active
    /// This editor shows the tools of the selection over several days.
    readonly property bool spanTools: inSpan && stream.toolsController === controller
    /// Why a tool does nothing on a selection over several days (a toast, the tools' tooltips).
    readonly property string spanReason: qsTr("Only copying works across several days")
    /// A selection over several days holds the selection: say that only copying works there. True
    /// when it did (the caller does nothing else).
    function spanRefuses(): bool {
        if (!inSpan)
            return false
        notice(spanReason, "info", null)
        return true
    }
    /// The end of a selection over several days moved into this day (StreamSelection.focusMoved):
    /// the keys come here, a touch UI is in the selection mode, `reveal` scrolls to `row`.
    function spanFocusHere(row: int, reveal: bool): void {
        if (!controller)
            return
        closeSuggestions()
        if (touch) {
            DeviceInfo.hideInputMethod()
            selectionMode = true
        }
        if (!list.activeFocus)
            list.forceActiveFocus()
        if (reveal)
            ensureRowVisible(row)
    }

    // ---- Page properties (docs/UI.md "Page properties") ----------------------------------------------
    /// The properties panel ("Właściwości") is expanded: a device setting in the app (PageView and
    /// the journal days bind it to AppSettings.propertiesExpanded and store it when it is
    /// switched: propertiesExpandedToggled()), folded by default.
    property bool propertiesExpanded: false
    signal propertiesExpandedToggled(bool expanded)
    /// The editor's list header shows the page's tags and aliases as chips while the panel is
    /// folded (PageView's ContentPageHeader): the folded panel's preview then leaves them out.
    property bool headerShowsListChips: false
    function setPropertiesExpanded(on: bool): void {
        if (propertiesExpanded === on)
            return
        propertiesExpanded = on
        propertiesExpandedToggled(on)
    }
    /// The first property of a page that shows none (PropertiesPanel's add row above the blocks).
    property bool propertyDraft: false
    /// A focus request for the panel: { key, part: "value" | "key" | "row" } for the row of `key`,
    /// { part: "add", source? } for the add row — taken by the row or panel once it exists (a
    /// property just added is a new row).
    property var propertyFocus: null
    /// The panel's field (or row) with the keyboard: kept above the on-screen keyboard
    /// (ensureEditVisible()).
    property Item editingProperty: null
    /// The type an empty value is edited as, chosen in a row's menu this session: { key: type }.
    property var propertyTypeOverride: ({})
    function setPropertyTypeOverride(key: string, type: string): void {
        propertyTypeOverride = Js.withKey(propertyTypeOverride, key, type)
    }
    /// "Dodaj właściwość" (Ctrl+;, the page menu, the palette): the add row of the shown block new
    /// properties go to (zoomed out first), or — that block is not shown (a page without
    /// properties, a frontmatter holding only the note mode, also above shown `key::` lines) —
    /// the draft above the blocks.
    function addProperty(): void {
        if (!controller || controller.sourceMode || !controller.loaded)
            return
        if (controller.zoomBlock >= 0)
            controller.zoomTo(-1)
        closeSuggestions()
        if (controller.editingRow >= 0)
            controller.stopEditing()
        const row = controller.pageProperties.addRow
        if (row >= 0) {
            setPropertiesExpanded(true)
            propertyFocus = { part: "add", source: controller.pageProperties.addTarget }
            positionLater.row = row
            positionLater.restart()
        } else {
            propertyDraft = true
            topLater.restart()
        }
    }

    // The title above the properties panel (BlockDelegate trades their places on screen): set by
    // those two delegates.
    readonly property bool titleFirst: controller !== null && controller.titleRow > 0 && controller.zoomBlock < 0
    property real titleHeight: 0
    property real propertiesHeight: 0
    /// The zoomed row the view last followed (onZoomChanged in the controller's Connections).
    property int shownZoom: -1

    /// The next / previous visible row in on-screen order (the title above the properties).
    function visualNext(row: int): int {
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
    function visualPrevious(row: int): int {
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
    final property point pendingClick: Qt.point(0, 0)

    // Fonts of the block kinds: body text and headings (outline: levels 1-3, deeper levels use
    // the body size in bold; document: larger, relative to the text size, levels 1-4). Fixed
    // pixel line heights, see Theme.lineHeightPx.
    function headingSize(level: int): int {
        if (documentMode) {
            if (level < 1 || level > 4)
                return fontSize
            // headingCap compresses the steps above the body size (the levels stay apart).
            const top = fontSize * 1.75
            const k = headingCap > fontSize && headingCap < top ? (headingCap - fontSize) / (top - fontSize) : 1
            const step = level === 1 ? 1.75 : level === 2 ? 1.45 : level === 3 ? 1.25 : 1.1
            return Math.round(fontSize + fontSize * (step - 1) * k)
        }
        return level === 1 ? Theme.text2xl : level === 2 ? Theme.textXl : level === 3 ? Theme.textLg : fontSize
    }
    function headingLineHeight(level: int): int {
        if (documentMode)
            return level >= 1 && level <= 4 ? Theme.lineHeightPx(headingSize(level), 1.3) : lineHeight
        return level >= 1 && level <= 3 ? Theme.lineHeightPx(headingSize(level), 1.4) : lineHeight
    }
    /// Baseline of the first line (from the top of a text item) for a heading level (0 = body);
    /// measured on hidden probes laid out exactly like the blocks, so bullets, checkboxes and
    /// fold arrows sit on the first line with any font.
    function firstBaseline(level: int): real {
        switch (level) {
        case 1: return probeH1.firstBaselineY
        case 2: return probeH2.firstBaselineY
        case 3: return probeH3.firstBaselineY
        case 4: return probeH4.firstBaselineY
        default: return probeBody.firstBaselineY
        }
    }
    function xHeight(level: int): real {
        switch (level) {
        case 1: return probeH1.xHeight
        case 2: return probeH2.xHeight
        case 3: return probeH3.xHeight
        case 4: return probeH4.xHeight
        default: return probeBody.xHeight
        }
    }
    /// Vertical centre of the x-height of the first line: where bullets and checkboxes go.
    function firstLineCenter(level: int): real {
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
        done: Theme.fgFaint, priority: Theme.dangerText, text: Theme.fg
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

    /// False once the editor is being destroyed (the deferred calls below check it).
    property bool alive: true
    Component.onDestruction: alive = false

    // Deferred work (instead of Qt.callLater, whose closures are not compiled ahead of time): the
    // Later calls below — posted events like Qt.callLater's, restarted: repeated requests before
    // they run are one, with the last values — and they check `alive` (a day of the journal stream
    // scrolled out of view, the page closed meanwhile).
    /// ensureEditVisible() once the event loop is idle.
    function laterEnsureEditVisible(): void {
        editVisibleLater.restart()
    }
    /// The reader moved the page (a page view's own scrolling): the list no longer keeps its top
    /// in view while rows above grow.
    function markUserMoved(): void {
        list.userMoved = true
    }
    /// revealRow(row) once the event loop is idle.
    function laterRevealRow(row: int): void {
        revealLater.target = row
        revealLater.restart()
    }
    Later {
        id: editVisibleLater
        onTriggered: if (editor.alive) editor.ensureEditVisible()
    }
    Later {
        id: revealLater
        property int target: -1
        onTriggered: if (editor.alive) editor.revealRow(revealLater.target)
    }
    /// The list to its beginning.
    Later {
        id: topLater
        onTriggered: if (editor.alive) list.positionViewAtBeginning()
    }
    /// The list to `row` (ListView.Contain) while there is a page.
    Later {
        id: positionLater
        property int row: -1
        onTriggered: {
            if (!editor.alive || !editor.controller)
                return
            list.positionViewAtIndex(positionLater.row, ListView.Contain)
        }
    }
    /// ensureRowVisible(): the list to `row` when it has no delegate there.
    Later {
        id: rowVisibleLater
        property int row: -1
        onTriggered: {
            const row = rowVisibleLater.row
            if (!editor.alive || !editor.controller || row < 0 || row >= list.count)
                return
            if (list.itemAtIndex(row) === null)
                list.positionViewAtIndex(row, ListView.Contain)
        }
    }

    // ---- Editing helpers called by delegates and the inline editors ------------------------------

    /// Start editing `row` at `cursor` (-1 = end) and give it the keyboard.
    function startEditing(row: int, cursor: int): void {
        if (!controller || row < 0)
            return
        closeSuggestions()
        findKeepsKeyboard = false
        clearFindSelection()
        const again = controller.editingRow === row
        controller.startEditing(row, cursor)
        if (again && activeEditor && activeEditor.row === row)
            activeEditor.ensureFocus()
        ensureRowVisible(row)
    }

    // ---- The find bar (FindBar, NoteFind; docs/UI.md "Find and replace") ---------------------------
    /// The note's find bar while it is open here: rendered blocks and inline editors tint its matches.
    final property NoteFind find: null
    // (final, not readonly: the find bar and the inline editors look it up through `editor`.)
    final property color findHitColor: Theme.link.alpha(0.28)
    /// The block going to a match does not take the keyboard from the bar (until it is tapped).
    property bool findKeepsKeyboard: false
    /// The match [findSelectionFrom, findSelectionTo) its block `findSelectionRow` is to select when
    /// its editor is there; row -1 again then (none). findSelectionChanged() on every change.
    property int findSelectionRow: -1
    property int findSelectionFrom: 0
    property int findSelectionTo: 0
    signal findSelectionChanged()
    function setFindSelection(row: int, from: int, to: int): void {
        findSelectionRow = row
        findSelectionFrom = from
        findSelectionTo = to
        findSelectionChanged()
    }
    function clearFindSelection(): void {
        setFindSelection(-1, 0, 0)
    }

    /// Show a match (NoteFind.targetRequested): out of folds and zooms, scrolled to, selected in its
    /// block's inline editor — or, where that cannot select it (a table, a list marker), the block
    /// selected and flashed. Source mode: selected in the text.
    function showFindTarget(target: var, keepKeyboard: bool): void {
        if (!controller)
            return
        closeSuggestions()
        findTarget.map = target
        if (findTarget.map.source) {
            const source = sourceLoader.item as SourceEditor
            if (source)
                source.showRange(findTarget.map.sourceFrom, findTarget.map.sourceTo, keepKeyboard)
            return
        }
        const row = findTarget.rowOf()
        if (row < 0)
            return
        controller.reveal(row)
        list.userMoved = true
        const editable = findTarget.map.editable === true
        if (editable) {
            findKeepsKeyboard = keepKeyboard
            controller.clearSelection()
            controller.startEditing(row, findTarget.map.from)
            setFindSelection(row, findTarget.map.from, findTarget.map.to)
        } else {
            findKeepsKeyboard = false
            clearFindSelection()
            controller.stopEditing()
            controller.selectedRow = row
        }
        findTargetLater.row = row
        findTargetLater.editable = editable
        findTargetLater.restart()
    }
    /// The match showFindTarget() was asked for (NoteFind.targetRequested's map).
    MapHolder {
        id: findTarget
        function rowOf(): int { return findTarget.map.row }
    }
    Later {
        id: findTargetLater
        property int row: -1
        property bool editable: false
        onTriggered: {
            if (!editor.alive)
                return
            const row = findTargetLater.row
            list.positionViewAtIndex(row, ListView.Contain)
            list.forceLayout()
            const item = list.itemAtIndex(row) as BlockDelegate
            if (!item)
                return
            if (editor.scroller)
                editor.scroller.ensureVisible(item, Qt.rect(0, 0, item.width, item.height))
            if (!findTargetLater.editable)
                item.flash()
            else if (editor.activeEditor && editor.activeEditor.row === row)
                editor.ensureVisible(editor.activeEditor, editor.activeEditor.caretRect)
        }
    }
    /// Source mode's text: the replaces go through its own undo history.
    function replaceInSource(ranges: list<var>, text: string): void {
        const source = sourceLoader.item as SourceEditor
        if (source)
            source.replaceRanges(ranges, text)
    }
    /// The caret in source mode, where the bar starts searching (-1: not in source mode).
    function sourcePosition(): int {
        return sourceTextEdit ? sourceTextEdit.cursorPosition : -1
    }
    /// The find bar closed: the keyboard comes back to what was edited. On a touch screen a block
    /// entered only to show a match is left again: no keyboard pops up for it.
    function findClosed(): void {
        findKeepsKeyboard = false
        clearFindSelection()
        if (!controller)
            return
        if (controller.sourceMode) {
            if (sourceLoader.item)
                (sourceLoader.item as SourceEditor).focusEditor()
        } else if (controller.editingRow >= 0 && !touch) {
            if (activeEditor)
                activeEditor.ensureFocus()
        } else {
            if (touch)
                controller.stopEditing()
            list.forceActiveFocus()
        }
    }

    /// Scroll to `row` (unfolding / leaving a zoom as needed), select it and flash it — for routes
    /// with `line` / `blockId` / `heading` (revealRow(controller.rowOfBlockId(route.blockId))), links
    /// to a block or heading of the page, the side panel's outline.
    function revealRow(row: int): void {
        if (!controller || row < 0)
            return
        controller.reveal(row)
        controller.stopEditing()
        controller.selectedRow = row
        // The header (page title) settling its height must not scroll back to the top.
        list.userMoved = true
        revealedLater.row = row
        revealedLater.restart()
        list.forceActiveFocus()
    }
    Later {
        id: revealedLater
        property int row: -1
        onTriggered: {
            if (!editor.alive)
                return
            const row = revealedLater.row
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
        }
    }
    /// A link's block (`blockId`) or heading is not on this page (nor anywhere): say so. `ref`: the
    /// block was named by a ((reference)), which no page of the notebook has.
    function noticeMissing(blockId: string, heading: string, ref: bool): void {
        if (blockId !== "" && ref)
            notice(qsTr("No block in the notebook has the id %1").arg(blockId), "info", null)
        else if (blockId !== "")
            notice(qsTr("Block ^%1 is not on this page").arg(blockId), "info", null)
        else if (heading !== "")
            notice(qsTr("Heading “%1” is not on this page").arg(heading), "info", null)
    }
    /// noticeMissing() for PageController.activateLink's {action: "notFound", kind, name}.
    function noticeNotFound(r: var): void {
        notFound.map = r
        const kind = notFound.kind()
        const name = notFound.name()
        if (kind === "task") {
            // A ⛔ id no task carries (docs/SYNTAX.md §5): it blocks nothing.
            notice(qsTr("No task in the notebook has the id %1").arg(name), "info", null)
            return
        }
        noticeMissing(kind === "heading" ? "" : name, kind === "heading" ? name : "", kind === "ref")
    }
    MapHolder {
        id: notFound
        function kind(): string { return notFound.map.kind }
        function name(): string { return notFound.map.name }
    }

    // ---- Copying links (docs/SYNTAX.md §2 "Copied links") -----------------------------------------

    /// Copy a link to `row` — kind "blockRef" ((id)), "blockLink" [[Page#^id]], "heading"
    /// [[Page#Heading]], "embed", "page" [[Page]] (PageController.copyLink) — and say so: "Skopiowano
    /// …", or why nothing was copied. True when copied.
    function copyLink(row: int, kind: string): bool {
        if (!controller)
            return false
        // A word the keyboard is still composing becomes text first (an id resets the text).
        if (activeEditor && activeEditor.row === row)
            EditorInput.commitPreedit(activeEditor)
        else if (editingTable && editingTable.block && editingTable.block.index === row && editingTable.cellInput)
            EditorInput.commitPreedit(editingTable.cellInput)
        copied.map = controller.copyLink(row, kind)
        const ok = copied.map.ok === true
        const reason = copied.reason()
        if (ok)
            notice(qsTr("Copied %1").arg(copied.map.text), "success", null)
        else
            notice(copyFailure(reason), reason === "noClipboard" || reason === "noId" ? "error" : "info", null)
        return ok
    }
    /// The result of the last copy ({ ok, text, reason, count }).
    MapHolder {
        id: copied
        /// `reason` ("" when missing: it is only compared with strings).
        function reason(): string { return copied.map.reason === undefined ? "" : copied.map.reason }
        function count(): int { return copied.map.count === undefined ? 0 : copied.map.count }
    }
    /// Why copyLink() copied nothing (PageController.copyLink's reason).
    function copyFailure(reason: string): string {
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
    function currentRow(): int {
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
    function copyCurrentLink(kind: string): bool {
        if (!controller)
            return false
        if (kind === "page")
            return copyLink(-1, "page")
        if (controller.sourceMode) {
            notice(copyFailure("sourceMode"), "info", null)
            return false
        }
        const current = currentRow()
        if (current < 0) {
            notice(copyFailure("noRow"), "info", null)
            return false
        }
        if (kind !== "heading")
            return copyLink(current, kind)
        const heading = controller.headingRowFor(current)
        if (heading < 0) {
            notice(qsTr("There is no heading above this block"), "info", null)
            return false
        }
        return copyLink(heading, "heading")
    }

    // ---- Selecting and copying blocks (docs/UI.md "Selecting and copying") ------------------------

    /// Stop editing (the word being composed kept, the keyboard's suggestions closed) before a
    /// block selection takes over; the list gets the keyboard.
    function leaveEditingForSelection(): void {
        if (activeEditor)
            EditorInput.commitPreedit(activeEditor)
        closeSuggestions()
        if (controller && controller.editingRow >= 0)
            controller.stopEditing()
        list.forceActiveFocus()
    }
    /// Select `row` (with its subtree) and, on touch UIs, enter the selection mode (the keyboard
    /// goes away, the selection tools dock where the editing toolbar was).
    function selectBlock(row: int): void {
        if (!controller || row < 0 || row >= controller.blocks.count)
            return
        leaveEditingForSelection()
        if (touch)
            DeviceInfo.hideInputMethod()
        controller.selectedRow = row
        selectionMode = touch && controller.selectionCount > 0
        ensureRowVisible(row)
    }
    function leaveSelectionMode(): void {
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
    function blockTapped(row: int): bool {
        return blockTappedWith(row, EditorInput.keyboardModifiers())
    }
    /// blockTapped() with the tap's own `modifiers`.
    function blockTappedWith(row: int, modifiers: int): bool {
        // The press that ended a drag selection is no tap (its release may be on a block).
        if (drag.selected)
            return true
        if (!controller || row < 0)
            return false
        const mods = modifiers
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
        else if (controller.blocks.viewAt(row) !== "frontmatter"
                 && !(stream !== null && stream.extendTo(controller, row)))
            controller.extendSelectionTo(row)
        return true
    }
    /// Ctrl+click on `row` (a tap with "Pojedynczo"): that block with its children into the
    /// selection or out of it, the rest stays (gaps allowed); the block being edited joins it.
    function toggleBlock(row: int): void {
        if (!controller || row < 0 || row >= controller.blocks.count || controller.blocks.viewAt(row) === "frontmatter")
            return
        if (activeEditor)
            EditorInput.commitPreedit(activeEditor)
        closeSuggestions()
        const result = controller.toggleSelected(row)
        if (result === "inParent")
            notice(qsTr("This block is selected with the block it belongs to"), "info", null)
        if (!list.activeFocus)
            list.forceActiveFocus()
        if (touch && controller.selectionCount > 0) {
            DeviceInfo.hideInputMethod()
            selectionMode = true
        }
    }
    /// Indenting or moving the selection: only without gaps (blocks picked one by one may leave
    /// some) — else a toast says why. True when it may go on.
    function selectionMovable(): bool {
        if (!controller || controller.selectionContiguous)
            return true
        notice(gapReason, "info", null)
        return false
    }
    /// Shift+click on `row`: the selection from the block being edited or the selection's anchor
    /// to it (a block alone when there is neither).
    function extendSelectionTo(row: int): void {
        if (!controller || row < 0)
            return
        // From a block selected or edited in another day of the journal stream (a selection over
        // several days), or back into the day its anchor is in.
        if (stream !== null && controller.blocks.viewAt(row) !== "frontmatter" && stream.extendTo(controller, row))
            return
        const anchor = controller.selectionAnchorRow >= 0 ? controller.selectionAnchorRow : controller.editingRow
        leaveEditingForSelection()
        controller.selectRange(anchor >= 0 ? anchor : row, row)
        ensureRowVisible(row)
    }
    /// Ctrl+A with blocks: every block of the page (in a zoom the zoomed subtree).
    function selectAllBlocks(): void {
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
    function selectionCopyFailure(reason: string): string {
        switch (reason) {
        case "tooLarge": return qsTr("The selection is too large to copy. Select fewer blocks.")
        case "empty": return qsTr("Nothing to copy")
        default: return copyFailure(reason)
        }
    }
    /// Copy the selection (Markdown with HTML, or `plainText`) and say so. True when copied.
    function copySelection(plainText: bool): bool {
        if (!controller)
            return false
        copied.map = inSpan ? stream.copy(plainText) : controller.copySelection(plainText)
        const ok = copied.map.ok === true
        const count = copied.count()
        const reason = copied.reason()
        if (ok && inSpan)
            notice(qsTr("Copied %n block(s) from several days", "", count), "success", null)
        else if (ok)
            notice(qsTr("Copied %n block(s)", "", count), "success", null)
        else
            notice(selectionCopyFailure(reason), reason === "empty" ? "info" : "error", null)
        return ok
    }
    function cutSelection(): bool {
        if (!controller || spanRefuses())
            return false
        const before = controller.undoToken() + "" // (a typed copy: passed on after other calls)
        copied.map = controller.cutSelection()
        const ok = copied.map.ok === true
        const count = copied.count()
        const reason = copied.reason()
        if (ok) {
            notice(qsTr("Cut %n block(s)", "", count), "success", undoAction(before))
            if (controller.selectedRow >= 0)
                ensureRowVisible(controller.selectedRow)
        } else {
            notice(selectionCopyFailure(reason), reason === "empty" ? "info" : "error", null)
        }
        return ok
    }
    function deleteSelection(): void {
        if (!controller || spanRefuses())
            return
        const count = Math.max(1, controller.selectionCount)
        const multiple = controller.multiSelection
        const before = controller.undoToken() + "" // (typed copies: passed on after other calls)
        const focus = controller.deleteSelection() + 0
        if (multiple)
            notice(qsTr("Deleted %n block(s)", "", count), "info", undoAction(before))
        if (focus >= 0)
            ensureRowVisible(focus)
    }
    /// Ctrl+V with blocks selected: the clipboard's text replaces them.
    function pasteOverSelection(): void {
        if (!controller || spanRefuses())
            return
        pastedNow.map = controller.pasteOverSelection()
        const row = resultRow(pastedNow.map)
        if (row < 0) {
            notice(qsTr("Nothing to paste"), "info", null)
        } else {
            ensureRowVisible(row)
            offerPastedPictures(pastedNow.map)
        }
    }
    /// Copy the exact characters [from, to) of the edited block `edit` (Ctrl+C, Ctrl+X, the text
    /// menu: TextEdit's own copy turns no-break spaces into spaces); `cut` removes them.
    function copyEditSelection(edit: InlineEditor, cut: bool): bool {
        if (!controller || !edit || !differs(edit.selectionStart, edit.selectionEnd))
            return false
        const from = edit.selectionStart + 0 // (typed copies: passed on after other calls)
        const to = edit.selectionEnd + 0
        copied.map = controller.copyEditText(edit.row, from, to, cut)
        if (copied.map.ok !== true) {
            notice(selectionCopyFailure(copied.reason()), "error", null)
            return false
        }
        if (cut)
            edit.remove(from, to)
        return true
    }
    /// Paste into the edited block `edit` at its selection: Markdown splits into blocks, HTML from
    /// other apps comes as Markdown (PageController.pasteClipboard). False when the clipboard has
    /// no text (then nothing happens).
    function pasteIntoEdit(edit: InlineEditor): bool {
        if (!controller || !edit)
            return false
        if (edit.preeditText !== "")
            EditorInput.commitPreedit(edit)
        pastedNow.map = controller.pasteClipboard(edit.row, edit.selectionStart, edit.selectionEnd)
        if (resultRow(pastedNow.map) < 0)
            return false
        offerPastedPictures(pastedNow.map)
        return true
    }
    /// Pictures from the internet a paste brought (PageController.pasteClipboard): a picture's
    /// address alone (a browser's "Kopiuj adres obrazu") — "Zapisz w notatniku" makes it the
    /// picture and stores it in the notebook (urlToPicture); pictures of pasted HTML — stored
    /// on "Zapisz w notatniku" (downloadWebPictures). Offered on a toast; nothing is fetched before.
    function offerPastedPictures(r: var): void {
        const ctl = controller
        if (!ctl || !r)
            return
        pasted.map = r
        if (pasted.map.pictureUrl) {
            const action = toastAction("urlToPicture", qsTr("Save to notebook"))
            action.controller = ctl
            action.path = ctl.path
            action.row = pasted.map.row
            action.from = pasted.map.pictureFrom
            action.to = pasted.map.pictureTo
            action.url = pasted.map.pictureUrl
            notice(qsTr("Picture address pasted"), "info", action)
        } else {
            pastedUrls = pasted.webPictures()
            if (pastedUrls.length === 0)
                return
            const action = toastAction("downloadWeb", qsTr("Save to notebook"))
            action.controller = ctl
            action.path = ctl.path
            action.urls = pastedUrls
            notice(qsTr("Pasted %n picture(s) from the internet", "", pastedUrls.length), "info", action)
        }
    }
    /// The paste offerPastedPictures() is about (PageController.pasteClipboard's result).
    MapHolder {
        id: pasted
        function webPictures(): list<string> { return pasted.map.webPictures }
    }
    /// The pictures from the internet of the paste offerPastedPictures() is about.
    property list<string> pastedUrls
    /// A paste's result, passed on to offerPastedPictures().
    MapHolder { id: pastedNow }

    /// The row drawn at (x, y) of the list's content item; -1 when none (the header, the footer, a
    /// gap). Not ListView.indexAt(): it goes by the rows' layout and ignores the Translate that
    /// draws the page's title above its properties (titleFirst); mapping into each delegate goes
    /// through it. (Nor contentItem.childAt(): the list's own highlight item covers the current row.)
    function rowAt(x: real, y: real): int {
        for (let i = EditorInput.childCount(listContent) - 1; i >= 0; --i) {
            const item = EditorInput.childAt(listContent, i) as BlockDelegate
            if (!item || !item.visible)
                continue
            mapPoint(listContent, item, x, y)
            if (item.contains(mapped))
                return item.index
        }
        return -1
    }

    /// A click on the rendered text of `row` at (x, y) in that text's coordinates (`modifiers`: the
    /// click's, see blockTapped()).
    function editAt(row: int, x: real, y: real): void {
        editAtWith(row, x, y, EditorInput.keyboardModifiers())
    }
    /// editAt() with the click's own `modifiers`.
    function editAtWith(row: int, x: real, y: real, modifiers: int): void {
        if (blockTappedWith(row, modifiers))
            return
        pendingClickRow = row
        pendingClick = Qt.point(x, y)
        startEditing(row, -1)
    }

    function stopEditing(select: bool): void {
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
    function undoStep(redo: bool): void {
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
            DeviceInfo.hideInputMethod()
            selectionMode = true
        }
        if (!list.activeFocus)
            list.forceActiveFocus()
    }

    function toggleSourceMode(): void {
        if (!controller)
            return
        closeSuggestions()
        controller.save()
        controller.sourceMode = !controller.sourceMode
    }

    function ensureRowVisible(row: int): void {
        rowVisibleLater.row = row
        rowVisibleLater.restart()
    }

    /// Keep `rect` of `item` (e.g. the caret) inside the visible part of the list — or of the
    /// list scrolling the editor (`scroller`).
    function ensureVisible(item: Item, rect: rect): void {
        const height = rect.height
        if (!item || !list || !editor.alive)
            return
        if (editor.scroller) {
            editor.scroller.ensureVisible(item, rect)
            return
        }
        mapPoint(item, listContent, rect.x, rect.y)
        const p = mapped
        const margin = editor.lineHeight
        const bottomLimit = list.height - list.bottomMargin
        if (p.y - list.contentY < margin)
            list.contentY = Math.max(list.originY - list.topMargin, p.y - margin)
        else if (p.y + height - list.contentY > bottomLimit - margin)
            list.contentY = Math.min(p.y + height - bottomLimit + margin,
                                     Math.max(list.originY, list.contentHeight + list.originY - list.height + list.bottomMargin))
    }

    /// Keep the caret of an inline editor visible (called after layout settled). Not while the
    /// keyboard slides (the view changes size every frame): once it has settled.
    function ensureCaretVisible(input: TextEdit): void {
        // (A TextEdit: an InlineEditor passes itself, which compiled code cannot do as its own type.)
        const edit = input as InlineEditor
        if (!edit || !edit.activeFocus || KeyboardInsets.animating)
            return
        if (editor.typewriter)
            centerInView(edit, edit.caretRect)
        else
            ensureVisible(edit, edit.caretRect)
    }
    /// Typewriter scrolling: `rect` of `item` (the caret) to the middle of the visible part of
    /// the list — or of the list scrolling the editor — as far as the content goes.
    function centerInView(item: Item, rect: rect): void {
        const height = rect.height
        if (!item || !list || !editor.alive)
            return
        if (editor.scroller) {
            // (Every scroller has centerInView now: EditorScroller.)
            editor.scroller.centerInView(item, rect)
            return
        }
        mapPoint(item, listContent, rect.x, rect.y)
        const p = mapped
        const visibleHeight = list.height - list.bottomMargin
        const top = list.originY - list.topMargin
        const bottom = Math.max(top, list.contentHeight + list.originY - list.height + list.bottomMargin)
        const target = Math.max(top, Math.min(bottom, p.y + height / 2 - visibleHeight / 2))
        if (Math.abs(target - list.contentY) >= 1)
            list.contentY = target
    }
    /// Keep what is being edited visible: the caret of the block, else the table cell.
    function ensureEditVisible(): void {
        if (KeyboardInsets.animating || !editor.alive)
            return
        const cell = editingTable ? editingTable.cellInput : null
        if (activeEditor && activeEditor.activeFocus)
            ensureVisible(activeEditor, activeEditor.caretRect)
        else if (cell)
            ensureVisible(cell, Qt.rect(0, 0, cell.width, cell.height))
        else if (editingProperty && editingProperty.activeFocus)
            ensureVisible(editingProperty, Qt.rect(0, 0, editingProperty.width, editingProperty.height))
    }
    // The view shrinks with the keyboard (the shell lifts it): what is being edited stays
    // visible — once the keyboard has settled. (Growing, it covers nothing: the view stays where
    // the user scrolled it.)
    property real _lastHeight: 0
    onHeightChanged: {
        const shrank = height < _lastHeight - 0.5
        _lastHeight = height
        if (shrank && !autoHeight && (activeEditor !== null || editingTable !== null || editingProperty !== null))
            editVisibleLater.restart()
    }
    readonly property bool keyboardSliding: KeyboardInsets.animating
    onKeyboardSlidingChanged: {
        if (!keyboardSliding && KeyboardInsets.shown
                && (activeEditor !== null || editingTable !== null || editingProperty !== null))
            editVisibleLater.restart()
    }

    /// Edit cell (r, c) of the table in `row` (the cell's toolbar after an undo or redo, which
    /// closed the cell first: the table keeps its delegate, the cell opens again with the text the
    /// step left in it).
    function editTableCell(row: int, r: int, c: int): void {
        if (!controller || row < 0 || row >= controller.blocks.count)
            return
        list.forceLayout()
        const item = list.itemAtIndex(row) as BlockDelegate
        const table = item ? item.contentItem as TableBlockView : null
        if (table)
            table.startCell(r, c)
    }

    /// The toast after "/Tabela z CSV…" made a table (`truncated`: rows, columns or text left out).
    function csvInserted(truncated: bool): void {
        if (truncated)
            notice(qsTr("The CSV was too large: the table holds its first rows and columns."), "info", null)
    }
    /// Show the filter row of the table in `row` with the keys in column `c`'s field (the block
    /// menu's "Filtruj wiersze" / "Filtruj według tej kolumny…": once `menu` is gone).
    function openTableFilter(row: int, c: int, menu: T.Popup): void {
        if (!controller || row < 0 || row >= controller.blocks.count)
            return
        controller.setTableFilterOpen(row, true)
        tableFilterLater.row = row
        tableFilterLater.column = c
        // A menu closing gives the keys back to what had them: the field takes them after that.
        if (menu && (menu.opened || menu.visible))
            tableFilterLater.menu = menu
        else
            tableFilterLater.restart()
    }
    /// openTableFilter(): the filter's field takes the keys (once `menu` is closed).
    Later {
        id: tableFilterLater
        property int row: -1
        property int column: 0
        property T.Popup menu: null
        onTriggered: {
            if (!editor.alive)
                return
            list.forceLayout()
            const item = list.itemAtIndex(tableFilterLater.row) as BlockDelegate
            const table = item ? item.contentItem as TableBlockView : null
            if (table)
                table.openFilter(tableFilterLater.column)
        }
    }
    Connections {
        target: tableFilterLater.menu
        function onClosed() {
            tableFilterLater.menu = null
            tableFilterLater.restart()
        }
    }
    /// The table in `row` shows its filter row.
    function tableFiltering(row: int): bool {
        if (!controller || row < 0 || row >= controller.blocks.count)
            return false
        const item = list.itemAtIndex(row) as BlockDelegate
        const table = item ? item.contentItem as TableBlockView : null
        return table !== null && table.filtering
    }

    /// The same once `menu` is closed: a menu closing gives the keys back to what had them, which
    /// would end the cell's editing at once.
    function editTableCellOnceClosed(menu: T.Popup, row: int, r: int, c: int): void {
        cellLater.row = row
        cellLater.r = r
        cellLater.c = c
        if (menu.opened || menu.visible)
            cellLater.menu = menu
        else
            cellLater.restart()
    }
    Later {
        id: cellLater
        property int row: -1
        property int r: 0
        property int c: 0
        property T.Popup menu: null
        onTriggered: if (editor.alive) editor.editTableCell(cellLater.row, cellLater.r, cellLater.c)
    }
    Connections {
        target: cellLater.menu
        function onClosed() {
            cellLater.menu = null
            cellLater.restart()
        }
    }

    /// Ends the editing the touch toolbar is docked for — a block, a table cell, the source's
    /// keyboard — as its "Hide keyboard" does (the word being composed is kept): the shell's quick
    /// capture from the bottom navigation would otherwise open over it. The selection's tools stay.
    function endTouchEditing(): void {
        const bar = toolbarLoader.item as MobileEditToolbar
        if (bar && toolbarVisible && toolbarMode !== "selection")
            bar.hideKeyboard()
    }

    function releaseSourceKeyboard(): void {
        if (sourceLoader.item)
            (sourceLoader.item as SourceEditor).releaseKeyboard()
    }

    /// Alt+Enter while editing: follow the link at the caret (a path part of [[A/B/C]]: that
    /// part — PageController.linkAtCursor); `newPane` (Alt+Shift+Enter on a wide window) in the side
    /// panel. "There is no link at the cursor" when none is there; nothing is typed either way.
    function followLinkAtCursor(row: int, position: int, newPane: bool): void {
        followLink(row, controller ? controller.linkAtCursor(row, position) : "", newPane)
    }
    /// Follow `url` (a link at a caret: a block's or a table cell's) as a click on it would.
    function followLink(row: int, url: string, newPane: bool): void {
        if (!controller)
            return
        if (url === "" || editsOnClick(url) || Js.startsWith(url, "task:"))
            notice(qsTr("There is no link at the cursor"), "info", null)
        else if (newPane)
            controller.activateLink(row, url, true)
        else
            activateLink(row, url)
    }

    function activateLink(row: int, link: string): void {
        if (!controller)
            return
        activated.map = controller.activateLink(row, link, false)
        if (!activated.map.action)
            return
        const action = activated.action()
        if (action === "reminder") {
            dialogs.editReminder(row, activated.map.from, activated.map.to)
        } else if (action === "scrollTo") {
            revealRow(activated.map.row)
        } else if (action === "notFound") {
            noticeNotFound(activated.map)
        }
    }
    /// PageController.activateLink's result.
    MapHolder {
        id: activated
        function action(): string { return activated.map.action }
    }

    /// A new drawing at the caret `cursor` of `row` (-1: a block of its own after it), opened in the
    /// drawing editor (Rysunek: the slash command, the touch toolbar, the block menu).
    function insertDrawing(row: int, cursor: int): void {
        if (!controller)
            return
        drawing.map = controller.insertDrawing(row, cursor, cursor)
        if (drawing.map.path)
            controller.openDrawing(drawing.map.path)
    }
    MapHolder { id: drawing }

    /// Run the result of PageController.applySlashCommand / applyCompletion.
    function handleCommandResult(row: int, result: var): void {
        if (!result)
            return
        command.map = result
        const cursor = command.cursor()
        switch (command.action()) {
        case "reminder": dialogs.createReminder(row, false); break
        case "alarm": dialogs.createReminder(row, true); break
        case "due": dialogs.pickDate(row, "due"); break
        case "scheduled": dialogs.pickDate(row, "scheduled"); break
        case "recurrence": dialogs.pickRecurrence(row); break
        case "date": dialogs.pickJournalDate(row, cursor); break
        case "template": dialogs.pickTemplate(row); break
        case "image": dialogs.pickImage(row, cursor); break
        case "fileLink": dialogs.pickFileLink(row, cursor); break
        case "csvTable": dialogs.pickCsvTable(row); break
        case "webImage": dialogs.pickWebImage(row, cursor); break
        case "drawing": insertDrawing(row, cursor); break
        case "drawingOn": dialogs.pickDrawingBackdrop(row, cursor); break
        case "table":
            list.forceActiveFocus()
            break
        case "complete":
            completionLater.restart()
            break
        }
    }
    /// A slash command's or completion's result (PageController.applySlashCommand / applyCompletion).
    MapHolder {
        id: command
        function action(): string { return command.map.action === undefined ? "" : command.map.action }
        function cursor(): int { return command.map.cursor === undefined ? -1 : command.map.cursor }
    }
    Later {
        id: completionLater
        onTriggered: if (editor.alive) editor.updateCompletion(editor.activeEditor)
    }

    // ---- Autocomplete ------------------------------------------------------------------------

    /// Re-detect the completion trigger at the caret of `edit` (InlineEditor). A word being
    /// composed by the input method (Android keyboards compose every word) counts as typed.
    function updateCompletion(input: TextEdit): void {
        // (A TextEdit: an InlineEditor passes itself, which compiled code cannot do as its own type.)
        const edit = input as InlineEditor
        if (!edit || !edit.activeFocus || edit.readOnly || !edit.completions) {
            closeSuggestions()
            return
        }
        const plain = edit.plain() + ""
        const caret = edit.cursorPosition + 0 // (typed copies: TextEdit's own are looked into)
        const pre = edit.preeditText + ""
        detected.map = pre === "" ? suggestionModel.detect(plain, caret)
                                  : suggestionModel.detect(Js.slice(plain, 0, caret) + pre + Js.sliceFrom(plain, caret),
                                                           caret + pre.length)
        if (detected.map.mode === "") {
            closeSuggestions()
            return
        }
        suggestionModel.setTrigger(detected.map)
        if (suggestionModel.count === 0) {
            closeSuggestions()
            return
        }
        const popup = suggestionPopup()
        popup.trigger = detected.map
        popup.row = edit.row
        popup.target = edit
        popup.show()
    }

    /// SuggestionModel.detect()'s trigger.
    MapHolder { id: detected }

    function acceptSuggestion(index: int): void {
        const edit = suggestions ? suggestions.target : null
        if (!controller || !edit || index < 0 || index >= suggestionModel.count)
            return
        // The trigger counted the composed word: make it text before replacing it.
        chosenItem.map = suggestionModel.get(index)
        if (EditorInput.commitPreedit(edit))
            updateCompletion(edit)
        chosenTrigger.map = suggestions.trigger
        const row = suggestions.row + 0 // (a typed copy: passed on after other calls)
        closeSuggestions()
        chosenResult.map = controller.applyCompletion(row, chosenTrigger.map, chosenItem.map)
        handleCommandResult(row, chosenResult.map)
    }
    /// acceptSuggestion(): the suggestion, its trigger and what applying it says.
    MapHolder { id: chosenItem }
    MapHolder { id: chosenTrigger }
    MapHolder { id: chosenResult }

    // ---- Keyboard -----------------------------------------------------------------------------

    function isFirstLine(edit: InlineEditor): bool {
        return edit.caretRect.y < edit.lineHeightPx / 2
    }
    function isLastLine(edit: InlineEditor): bool {
        return edit.caretRect.y + edit.caretRect.height > edit.contentHeight - edit.lineHeightPx / 2
    }

    /// `x`: the caret's x in the editor's coordinates (blocks are indented differently).
    function goToRow(row: int, edge: string, x: real): void {
        if (row < 0)
            return
        if (controller.blocks.viewAt(row) === "table") {
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
    function isCommandKey(key: int, modifiers: int): bool {
        if ((modifiers & (Qt.ControlModifier | Qt.AltModifier)) !== 0)
            return true
        switch (key) {
        case Qt.Key_Return: case Qt.Key_Enter: case Qt.Key_Backspace: case Qt.Key_Delete:
        case Qt.Key_Tab: case Qt.Key_Backtab: case Qt.Key_Up: case Qt.Key_Down: case Qt.Key_Escape:
            return true
        }
        return false
    }

    /// Keys of an inline editor (InlineEditor.qml). Returns true when handled.
    function handleEditorKey(event: KeyEvent, input: TextEdit): bool {
        return handleEditorKeyCode(event.key, event.modifiers, input as InlineEditor)
    }
    /// handleEditorKey() for key `key` with `modifiers` (also the self-tests' keys).
    function handleEditorKeyCode(key: int, modifiers: int, edit: InlineEditor): bool {
        if (edit.preeditText !== "" && isCommandKey(key, modifiers))
            EditorInput.commitPreedit(edit)
        const row = edit.row
        const ctrl = (modifiers & Qt.ControlModifier) !== 0
        const shift = (modifiers & Qt.ShiftModifier) !== 0
        const alt = (modifiers & Qt.AltModifier) !== 0
        const multiline = edit.multiline

        // Suggestion popup navigation.
        if (suggestionsOpen) {
            switch (key) {
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
        const pasteKey = (ctrl && !alt && !shift && key === Qt.Key_V) || (shift && !ctrl && key === Qt.Key_Insert)
        if (pasteKey && !multiline && controller.clipboardHasImage()) {
            if (resultRow(controller.pasteImage(row, edit.selectionStart, edit.selectionEnd)) >= 0)
                return true
        }
        // Text: Markdown splits into blocks, HTML from other apps comes as Markdown, ids that
        // resolve elsewhere are dropped (PageController.pasteClipboard). Code and other multi-line
        // blocks take the text as it is (TextEdit's own paste).
        if (pasteKey && !multiline && pasteIntoEdit(edit))
            return true
        // Ctrl+Insert / Shift+Delete: copy / cut, exactly (like Ctrl+C / Ctrl+X).
        if (!alt && differs(edit.selectionStart, edit.selectionEnd)
                && ((ctrl && !shift && key === Qt.Key_Insert) || (shift && !ctrl && key === Qt.Key_Delete))) {
            copyEditSelection(edit, key === Qt.Key_Delete)
            return true
        }
        if (ctrl && !alt) {
            switch (key) {
            case Qt.Key_Z: undoStep(shift); return true
            case Qt.Key_Y: undoStep(true); return true
            case Qt.Key_E:
                if (editor.sourceModeShortcut !== "Ctrl+E")
                    return false
                toggleSourceMode()
                return true
            case Qt.Key_Period: controller.toggleFold(row); return true
            case Qt.Key_Return:
            case Qt.Key_Enter:
                // Ctrl+Shift+Enter: the status menu; Ctrl+Enter: the next status.
                if (shift)
                    openTaskStatusMenuFor(row, false)
                else
                    controller.cycleTask(row)
                return true
            case Qt.Key_C:
                // Ctrl+Shift+C: the block's reference ((id)); Ctrl+C copies the selected text
                // exactly (TextEdit's own copy turns no-break spaces into spaces).
                if (shift) {
                    if (editor.copyRefShortcut !== "Ctrl+Shift+C")
                        return false
                    copyCurrentLink("blockRef")
                    return true
                }
                if (!differs(edit.selectionStart, edit.selectionEnd))
                    return false
                copyEditSelection(edit, false)
                return true
            case Qt.Key_X:
                if (shift || !differs(edit.selectionStart, edit.selectionEnd))
                    return false
                copyEditSelection(edit, true)
                return true
            case Qt.Key_A:
                // Notion's second Ctrl+A: the whole block's text is selected already (or there is
                // none): every block of the page.
                if (shift)
                    return false
                if (edit.length === 0 || (edit.selectionStart === 0 && !differs(edit.selectionEnd, edit.length)))
                    selectAllBlocks()
                else
                    edit.selectAll()
                return true
            }
            return false
        }
        if (alt && !ctrl) {
            if (key === Qt.Key_Up) { controller.moveUp(row); ensureRowVisible(controller.editingRow); return true }
            if (key === Qt.Key_Down) { controller.moveDown(row); ensureRowVisible(controller.editingRow); return true }
            // Alt+Enter: follow the link (the path part) at the caret; with Shift in the side panel.
            // Always taken: it never types a line break.
            if (key === Qt.Key_Return || key === Qt.Key_Enter) {
                followLinkAtCursor(row, edit.cursorPosition, shift && !Theme.narrow)
                return true
            }
            return false
        }

        switch (key) {
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
                const kind = controller.blocks.kindAt(row)
                if (controller.blocks.taskStatusAt(row) !== "" && kind === "listItem") {
                    controller.removeTaskMarker(row)
                    return true
                }
                // A document's list item loses its marker (Obsidian), a nested one moves out.
                if (documentMode && kind === "listItem") {
                    const left = resultRow(controller.leaveList(row))
                    if (left >= 0)
                        ensureRowVisible(left)
                    return true
                }
                const merged = resultRow(controller.mergeWithPrevious(row))
                if (merged >= 0)
                    ensureRowVisible(merged)
                return true
            }
            return false
        case Qt.Key_Delete:
            if (!multiline && !differs(edit.cursorPosition, edit.length) && edit.selectedText === "") {
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
            if (!documentMode || controller.blocks.kindAt(row) === "listItem")
                controller.indent(row)
            return true
        case Qt.Key_Backtab:
            if (!documentMode || controller.blocks.kindAt(row) === "listItem")
                controller.outdent(row)
            return true
        case Qt.Key_Up:
            if (!shift && isFirstLine(edit)) {
                goToRow(visualPrevious(row), "bottom", caretX(edit))
                return true
            }
            // Shift+↑ on the first line: the text selection to the block's start, then the blocks.
            if (shift && isFirstLine(edit))
                return extendFromEdit(edit, -1)
            return false
        case Qt.Key_Down:
            if (!shift && isLastLine(edit)) {
                goToRow(visualNext(row), "top", caretX(edit))
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
    /// The caret's x of `edit` in the editor's coordinates.
    function caretX(edit: InlineEditor): real {
        mapPoint(edit, editor, edit.caretRect.x, 0)
        return mapped.x
    }
    /// A point mapped between items (mapPoint()), read here: what an Item's own mapToItem() /
    /// mapFromItem() returns cannot be looked into in compiled code, the editor's can.
    final property point mapped
    /// Map (x, y) of `from` (null: the scene) to `to` (null: the scene) into `mapped`.
    function mapPoint(from: Item, to: Item, x: real, y: real): void {
        mapped = editor.mapFromItem(from, x, y)
        mapped = editor.mapToItem(to, mapped.x, mapped.y)
    }
    /// The list's content item (its rows).
    readonly property Item listContent: list.contentItem

    function extendFromEdit(edit: InlineEditor, step: int): bool {
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
    function forwardToEditingRow(event: KeyEvent): bool {
        if (!controller || controller.editingRow < 0 || controller.sourceMode)
            return false
        if (activeEditor && activeEditor.activeFocus)
            return false
        // Typed text and keys that edit or move the caret; a lone modifier, PageUp/PageDown…
        // must not pull the view back.
        const printable = event.text !== "" && Js.charCodeAt(event.text, 0) >= 32 && Js.charCodeAt(event.text, 0) !== 127
        let editing = false
        switch (event.key) {
        case Qt.Key_Return: case Qt.Key_Enter: case Qt.Key_Backspace: case Qt.Key_Delete: case Qt.Key_Tab:
        case Qt.Key_Backtab: case Qt.Key_Left: case Qt.Key_Right: case Qt.Key_Up: case Qt.Key_Down:
            editing = true
        }
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
    function autoTextSafe(s: string): string {
        return s !== "" && Js.indexOf(s, "<") >= 0 ? TextHelper.escapeHtml(s) : s
    }

    /// Keys while blocks are selected (not editing): one block (Esc) or a range — Shift+↑/↓ extend
    /// or shrink it, Ctrl+A selects every block, Ctrl+C / Ctrl+X / Ctrl+V copy, cut and paste over
    /// it, Delete removes it, Tab / Shift+Tab, Alt+↑/↓ and Ctrl+Enter act on all of it.
    function handleSelectionKey(event: KeyEvent): bool {
        return handleSelectionKeyCode(event.key, event.modifiers)
    }
    /// handleSelectionKey() for key `key` with `modifiers` (also the self-tests' keys).
    function handleSelectionKeyCode(key: int, modifiers: int): bool {
        if (!controller)
            return false
        const row = controller.selectedRow
        const ctrl = (modifiers & Qt.ControlModifier) !== 0
        const shift = (modifiers & Qt.ShiftModifier) !== 0
        const alt = (modifiers & Qt.AltModifier) !== 0
        // Blocks from one end to another, or picked one by one (a block selected alone — with the
        // children it brings along — keeps the block's own keys: Ctrl+Enter changes it, not its
        // children).
        const range = row >= 0 && ((controller.selectionAnchorRow >= 0 && controller.selectionAnchorRow !== row)
                                   || controller.selectionPicked)
        // Blocks of several days: copying only (Ctrl+X, Ctrl+V, Delete say so in their functions).
        if (inSpan && ((ctrl && (key === Qt.Key_Return || key === Qt.Key_Enter))
                       || (!ctrl && (key === Qt.Key_Tab || key === Qt.Key_Backtab))
                       || (alt && (key === Qt.Key_Up || key === Qt.Key_Down)))) {
            spanRefuses()
            return true
        }
        if (ctrl) {
            switch (key) {
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
                    if (editor.copyRefShortcut !== "Ctrl+Shift+C")
                        return false
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
            if (key === Qt.Key_Down || key === Qt.Key_Up) {
                const first = key === Qt.Key_Down ? visualNext(-1) : controller.lastVisibleRow() + 0
                controller.selectedRow = first
                ensureRowVisible(first)
                return true
            }
            return false
        }
        if (shift && !alt) {
            switch (key) {
            case Qt.Key_Up:
            case Qt.Key_Down: {
                // Past the day's edge (or over several days already): the journal stream's.
                const step = key === Qt.Key_Down ? 1 : -1
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
            if (key === Qt.Key_Up) {
                if (!range) controller.moveUp(row); else if (selectionMovable()) controller.moveSelectionUp()
                ensureRowVisible(controller.selectedRow)
                return true
            }
            if (key === Qt.Key_Down) {
                if (!range) controller.moveDown(row); else if (selectionMovable()) controller.moveSelectionDown()
                ensureRowVisible(controller.selectedRow)
                return true
            }
            return false
        }
        switch (key) {
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
            if (controller.blocks.viewAt(row) === "table") {
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
    function editorFocusLost(): void {
        focusLostLater.restart()
    }
    Later {
        id: focusLostLater
        onTriggered: {
            if (!editor.alive || !editor.controller)
                return
            if (!editor.activeFocus && !editor.suggestionsOpen && !editor.dialogs.anyOpen)
                editor.controller.save()
        }
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
            zoomLater.restart()
        }
        // The draft's property is there (or another program wrote one): the panel shows it.
        function onPagePropertiesChanged() {
            if (editor.propertyDraft && editor.controller.pageProperties.addRow >= 0)
                draftLater.restart()
        }
        function onLoadingChanged() {
            if (!editor.controller.loading) {
                // Another page (or the same one read again): no draft or focus request of the last.
                editor.propertyDraft = false
                editor.propertyFocus = null
                list.userMoved = false
                list.showsTop = true
                // The editor may be gone by then (a day of the journal stream scrolled away).
                topLater.restart()
            }
        }
        function onSourceModeChanged() {
            if (!editor.controller.sourceMode)
                list.forceActiveFocus()
        }
        function onAttachmentFailed(name: string) {
            editor.notice(qsTr("Could not add “%1” to the notebook").arg(name), "error", null)
        }
        function onCsvTableInserted(row: int, truncated: bool) {
            editor.csvInserted(truncated)
        }
        function onCsvTableFailed(name: string) {
            editor.notice(qsTr("“%1” holds no table Brulion can read.").arg(name), "error", null)
        }
        function onWebPicturesDownloaded(stored: int, failed: list<string>) {
            if (failed.length > 0)
                editor.notice(qsTr("Could not save to the notebook: %1").arg(failed.join(", ")), "error", null)
            else if (stored > 0)
                editor.notice(qsTr("%n picture(s) saved to the notebook", "", stored), "success", null)
        }
    }
    /// A zoom changed while a block is edited (onZoomChanged): the block stays in view.
    Later {
        id: zoomLater
        onTriggered: {
            if (!editor.alive)
                return
            const row = editor.controller ? editor.controller.editingRow : -1
            if (row < 0 || row >= list.count)
                return
            // The rows the zoom showed or hid get their heights first: positioned before, the
            // view kept its place among the zoom's rows and then showed the page's top.
            list.forceLayout()
            list.positionViewAtIndex(row, ListView.Contain)
            list.forceLayout()
            editor.ensureEditVisible()
        }
    }
    Later {
        id: draftLater
        onTriggered: if (editor.alive) editor.propertyDraft = false
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
    function openMergeDialog(): void {
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
        // A title drawn above the page's properties (titleFirst) lies below them in the list: it
        // is shown also while the expanded panel puts its row out of the view.
        displayMarginEnd: editor.autoHeight ? 1000000 : editor.titleFirst ? Math.ceil(editor.propertiesHeight) : 0
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
        onBottomMarginChanged: if (editor.toolbarVisible) editVisibleLater.restart()
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
            // The first property of a page that shows none (addProperty()): only the add row.
            Loader {
                objectName: "propertyDraft"
                x: editor.contentX
                width: editor.contentWidth
                active: editor.propertyDraft && editor.controller !== null
                visible: active
                sourceComponent: Rectangle {
                    implicitHeight: draftPanel.implicitHeight + 2 * Theme.space1
                    radius: Theme.radius
                    color: Theme.bgSunken
                    border.width: 1
                    border.color: Theme.borderSubtle
                    PropertiesPanel {
                        id: draftPanel
                        x: Theme.space1
                        y: Theme.space1
                        width: parent.width - 2 * Theme.space1
                        editorItem: editor
                        controller: editor.controller
                        draft: true
                    }
                }
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
            editorItem: editor
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
            if (!editor.controller || editor.controller.selectedRow < 0 || !(event.modifiers & Qt.ControlModifier))
                return
            switch (event.key) {
            case Qt.Key_A: case Qt.Key_C: case Qt.Key_X: case Qt.Key_V: case Qt.Key_Z: case Qt.Key_Y:
            case Qt.Key_Return: case Qt.Key_Enter:
                event.accepted = true
            }
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
                editor.openBlockMenuAtScene(eventPoint.scenePosition.x, eventPoint.scenePosition.y)
            }
        }

        // The header (page title) gets its height after the first layout: start at the top.
        // (autoHeight: no scroll bar, the list never scrolls.)
        Component.onCompleted: {
            if (!editor.autoHeight)
                editor.scrollBar = scrollBarComponent.createObject(list, { list: list }) as T.ScrollBar
            topLater.restart()
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
        /// The block under the pointer (updateDragTarget()): of this editor's page or another day's.
        property PageController targetController: null
        property int targetRow: -1
    }
    /// Another day's editor under the pointer (updateDragTarget(); an Item: what the stream gives
    /// is not this file's own type for the compiler).
    property Item dragOther: null
    /// Blocks are being selected by dragging (tests read it).
    readonly property bool dragSelecting: drag.active

    /// A press at `scene` (scene coordinates) by a mouse, touchpad or stylus (`mouse`) or a finger.
    function dragPressed(scene: point, mouse: bool): void {
        drag.selected = false
        drag.active = false
        drag.armed = false
        drag.speed = 0
        drag.fixedRow = -1
        drag.scene = scene
        drag.mouse = mouse
        drag.startRow = -1
        if (!controller || controller.sourceMode)
            return
        if (scrollBar && scrollBar.visible) {
            mapPoint(null, scrollBar, drag.scene.x, drag.scene.y)
            if (scrollBar.contains(mapped))
                return
        }
        mapPoint(null, listContent, drag.scene.x, drag.scene.y)
        drag.startRow = rowAt(mapped.x, mapped.y)
    }
    function dragMoved(scene: point): void {
        drag.scene = scene
        if (!controller)
            return
        if (!drag.active) {
            if (drag.startRow < 0 || (!drag.mouse && !drag.armed))
                return
            updateDragTarget()
            if (drag.targetRow < 0 || (drag.targetController === controller && drag.targetRow === drag.startRow)
                    || controller.blocks.viewAt(drag.startRow) === "frontmatter")
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
    function dragReleased(): void {
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
    function updateDragSelection(): void {
        if (!drag.active || !controller || drag.fixedRow < 0)
            return
        updateDragTarget()
        const target = drag.targetController
        if (drag.targetRow < 0 || target.blocks.viewAt(drag.targetRow) === "frontmatter")
            return
        if (target !== controller)
            stream.extendFrom(controller, drag.fixedRow, target, drag.targetRow)
        else
            controller.selectRange(drag.fixedRow, drag.targetRow) // (back in this day: a span ends)
    }
    /// The block under the pointer (drag.targetController, drag.targetRow): this editor's nearest
    /// block, or, below or above this day of the journal stream, the block of the day there.
    function updateDragTarget(): void {
        drag.targetController = controller
        drag.targetRow = rowNear(drag.scene)
        if (stream === null || !scroller)
            return
        mapped = editor.mapFromItem(null, drag.scene.x, drag.scene.y)
        if (mapped.y >= 0 && mapped.y < editor.height)
            return
        dragOther = scroller.editorAt(drag.scene)
        const other = dragOther as BlockEditor
        if (!other || other === editor || !other.controller || !other.controller.loaded)
            return
        const row = other.rowNear(drag.scene)
        if (row >= 0) {
            drag.targetController = other.controller
            drag.targetRow = row
        }
    }
    /// The block at a scene point, the nearest one when the point is above or below the blocks (the
    /// header, the footer, outside a day of the journal stream) or outside the view.
    function rowNear(scene: point): int {
        const sceneX = scene.x
        const sceneY = scene.y
        if (!controller)
            return -1
        mapPoint(null, listContent, sceneX, sceneY)
        const y = Math.max(list.contentY + 1, Math.min(list.contentY + list.height - 1, mapped.y))
        let row = rowAt(contentX + 1, y)
        if (row >= 0)
            return row
        const header = list.headerItem
        const headerBottom = header ? header.y + header.height : list.originY
        if (y <= headerBottom) {
            row = visualNext(-1)
            while (row >= 0 && controller.blocks.viewAt(row) === "frontmatter")
                row = controller.nextVisibleRow(row) + 0 // (a typed copy: passed on in the loop)
            return row
        }
        return controller.lastVisibleRow()
    }
    /// Scroll near the view's top and bottom edges while selecting (the toolbar's top for the bottom).
    function updateAutoScroll(): void {
        if (!drag.active) {
            drag.speed = 0
            return
        }
        const host = dockHost
        mapPoint(null, host, drag.scene.x, drag.scene.y)
        const y = mapped.y
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
            mapped = editor.mapToItem(host, 0, 0)
            const dayTop = mapped.y
            const reach = stream !== null ? scroller.selectionReach : 0
            const past = reach > 0 ? top - reach : bottom
            const before = reach > 0 ? bottom + reach : top
            if ((drag.speed > 0 && dayTop + editor.height <= past) || (drag.speed < 0 && dayTop >= before))
                drag.speed = 0
        }
    }
    /// Scroll the editor (or the journal stream it is part of) by `dy`, within its content.
    function scrollBy(dy: real): void {
        if (scroller) {
            scroller.scrollBy(dy)
            return
        }
        const min = list.originY - list.topMargin
        const max = Math.max(min, list.contentHeight + list.originY - list.height + list.bottomMargin)
        list.contentY = Math.max(min, Math.min(max, list.contentY + dy))
    }
    /// A finger selecting must not flick the list (or the journal stream) meanwhile.
    function setScrollingHeld(held: bool): void {
        if (drag.holding === held || (held && drag.mouse))
            return
        drag.holding = held
        if (scroller)
            scroller.holdScrolling(held)
        else if (!autoHeight)
            list.interactive = !held
    }
    /// A long press on the text of `row` (touch UIs; not the block being edited): the selection
    /// mode with that block selected — or, already in it, the selection extended there; moving the
    /// finger without lifting it extends it further.
    function longPressText(row: int): void {
        if (!touch || !controller || row < 0 || controller.blocks.viewAt(row) === "frontmatter")
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
    function beginGripDrag(atStart: bool): void {
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
            onActiveChanged: {
                if (!active) {
                    editor.dragReleased()
                    return
                }
                const device = dragPointer.point.device
                editor.dragPressed(dragPointer.point.scenePosition,
                                   device === null || device.type !== PointerDevice.TouchScreen)
            }
            onPointChanged: if (active) editor.dragMoved(dragPointer.point.scenePosition)
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
            editor.mapPoint(fileDrop, editor.listContent, drop.x, drop.y)
            const row = editor.rowAt(editor.contentX + 1, editor.mapped.y)
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
            color: Theme.info.alpha(0.06)
            border.width: 2
            border.color: Theme.focusRing.alpha(0.6)
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
            editorItem: editor
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
    function closeSuggestions(): void {
        if (suggestions)
            suggestions.close()
    }
    Component {
        id: suggestionComponent
        SuggestionPopup {
            editorItem: editor
            model: suggestionModel
            onActivated: (index) => editor.acceptSuggestion(index)
        }
    }

    // ---- The block menu --------------------------------------------------------------------------
    /// BlockMenu for a block (made when first opened, one per editor; tests read it): a bullet's or
    /// number's right-click / long press, a right-click on a block, the touch toolbar's "Więcej".
    property BlockMenu blockMenu: null
    function openBlockMenu(row: int, item: Item, x: real, y: real): void {
        openCellBlockMenu(row, item, x, y, -2, 0, 0, 0, "")
    }
    /// openBlockMenu() from table cell (cellRow, cellColumn) — the header's: cellRow -1; -2: no
    /// cell — of a table of cellRows × cellColumns, the column aligned cellAlign: for the menu's
    /// Wiersz and Kolumna submenus.
    function openCellBlockMenu(row: int, item: Item, x: real, y: real, cellRow: int, cellColumn: int, cellRows: int,
                               cellColumns: int, cellAlign: string): void {
        if (!controller || row < 0 || row >= controller.blocks.count)
            return
        if (!blockMenu)
            blockMenu = blockMenuComponent.createObject(editor) as BlockMenu
        menuAbove = null
        closeSuggestions()
        // A word the keyboard is still composing becomes text before anything acts on the block.
        if (activeEditor)
            EditorInput.commitPreedit(activeEditor)
        blockMenu.openFor(row, cellRow, cellColumn, cellRows, cellColumns, cellAlign)
        // Above the docked toolbar and the keyboard (it flips above its item when needed).
        blockMenu.bottomMargin = menuBottomMargin()
        if (item)
            blockMenu.popup(item, x, y)
        else
            blockMenu.popup()
    }
    /// The block menu of a right-click at `scene` (scene coordinates) on `row`: the list's handler,
    /// the rendered text's when the click was on no date chip. None for the page properties.
    /// The block menu of a right-click on the list at (x, y) of the scene: of the row there.
    function openBlockMenuAtScene(x: real, y: real): void {
        mapPoint(null, listContent, x, y)
        openBlockMenuAt(rowAt(mapped.x, mapped.y), Qt.point(x, y))
    }
    function openBlockMenuAt(row: int, scene: point): void {
        openCellBlockMenuAt(row, scene, -2, 0, 0, 0, "")
    }
    /// openBlockMenuAt() from a table cell (see openCellBlockMenu()).
    function openCellBlockMenuAt(row: int, scene: point, cellRow: int, cellColumn: int, cellRows: int,
                                 cellColumns: int, cellAlign: string): void {
        const sceneX = scene.x
        const sceneY = scene.y
        if (!controller || row < 0 || row >= controller.blocks.count || controller.blocks.viewAt(row) === "frontmatter")
            return
        mapPoint(null, list, sceneX, sceneY)
        openCellBlockMenu(row, list, mapped.x, mapped.y, cellRow, cellColumn, cellRows, cellColumns, cellAlign)
    }
    /// The touch toolbar's "Więcej": the menu of the block being edited, right above the button.
    /// The keyboard goes (the menu takes the keys, which hides it on Android anyway): the menu
    /// follows the toolbar down into the room it leaves (followToolbarWith), every item shown.
    function openBlockMenuAbove(row: int, item: Item): void {
        if (!controller || row < 0)
            return
        if (!blockMenu)
            blockMenu = blockMenuComponent.createObject(editor) as BlockMenu
        blockMenu.openFor(row, -2, 0, 0, 0, "")
        const y = -blockMenu.openHeight() - Theme.space1
        openBlockMenu(row, item, 0, y)
        followToolbarWith(blockMenu, item, y)
        DeviceInfo.hideInputMethod()
    }
    /// The cell toolbar's "Więcej": the block menu of the table whose cell is being edited. A cell
    /// is no block editor that could go on under the menu: its editing ends (its text kept, the
    /// keyboard hidden), the table is selected — the block the menu acts on, the current block of
    /// the palette's commands afterwards — and the menu opens at the table.
    function openTableBlockMenu(table: TableBlockView): void {
        if (!controller || !table || !table.block)
            return
        // (Typed copies: passed on after other calls.)
        const row = table.block.index + 0
        const cellRow = table.editRow + 0
        const cellColumn = table.editColumn + 0
        const cellRows = table.cellRowCount() + 0
        const cellColumns = table.columns + 0
        const cellAlign = table.cellAlignOf(cellColumn) + ""
        if (table.cellInput)
            EditorInput.commitPreedit(table.cellInput)
        DeviceInfo.hideInputMethod()
        table.closeCell()
        controller.selectedRow = row
        list.forceActiveFocus()
        list.forceLayout()
        const item = list.itemAtIndex(row) as BlockDelegate
        openCellBlockMenu(row, item, item ? item.contentLeft : 0, 0, cellRow, cellColumn, cellRows, cellColumns, cellAlign)
    }
    Component {
        id: blockMenuComponent
        BlockMenu {
            editorItem: editor
        }
    }

    // ---- Pictures from the internet ------------------------------------------------------------
    /// Store the picture from the internet `url` — "" every one on the page — in the notebook
    /// (PageController.downloadWebPictures; the toast comes with webPicturesDownloaded).
    function downloadWebPictures(url: string): void {
        if (!controller)
            return
        const count = controller.downloadWebPictures(url)
        if (count < 0)
            notice(qsTr("Switch back to blocks (Ctrl+E) to save the pictures in the notebook"), "info", null)
        else if (count === 0 && url === "")
            notice(qsTr("This page has no pictures from the internet"), "info", null)
    }
    /// WebPictureMenu for a shown picture from the internet (EmbedView: a right-click or long
    /// press on it; "⋯" in its viewer). Created when first opened.
    property WebPictureMenu webPictureMenu: null
    property string webPictureMenuUrl: ""
    property int webPictureMenuRow: -1
    function openWebPictureMenu(row: int, url: string, host: string, item: Item, x: real, y: real): void {
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
    function openWebPictureViewer(row: int, picture: var): void {
        if (!controller || !picture)
            return
        embedInfo.map = picture
        if (embedInfo.map.state !== "ready")
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
    function webPictureEmbed(row: int, url: string): var {
        const item = list.itemAtIndex(row) as BlockDelegate
        if (!item)
            return ({})
        const count = embedsOf(item).length
        for (let i = 0; i < count; ++i) {
            embedInfo.map = embedsOf(item)[i]
            if (embedInfo.map.web && embedInfo.map.target === url)
                return embedsOf(item)[i]
        }
        return ({})
    }
    function embedsOf(item: BlockDelegate): list<var> {
        return item.embeds
    }
    /// An embed of a block (BlockListModel's embeds: a picture from the internet).
    MapHolder { id: embedInfo }
    function chooseWebPictureAction(action: string): void {
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
                notice(qsTr("Picture address copied"), "success", null)
            else
                notice(qsTr("Could not copy: the clipboard is not available"), "error", null)
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
            MapHolder {
                id: shownPicture
                map: pictureViewer.picture
                function target(): string { return shownPicture.map.target ? shownPicture.map.target : "" }
                function host(): string { return shownPicture.map.host ? shownPicture.map.host : "" }
            }
            onSaveRequested: {
                const url = shownPicture.target()
                pictureViewer.close()
                if (url !== "")
                    editor.downloadWebPictures(url)
            }
            onMenuRequested: (item) => {
                editor.openWebPictureMenu(editor.webPictureViewerRow, shownPicture.target(), shownPicture.host(), item, 0,
                                          item.height)
            }
        }
    }

    // ---- Token menus: dates, reminders, 🔁, priorities, days (DateChipMenu) ---------------------
    /// DateChipMenu for a token of rendered text (`link`: its date:, reminder:, recur:, prio: or day:
    /// link): a right-click or long press on it — in a block (BlockText: openDateMenu()), a query
    /// result or an embed of another block (openTokenMenuIn()), a table cell (openCellTokenMenu()).
    /// Created when first opened; what the menu's choice applies to is `dateMenuMode` with the
    /// values below (popupTokenMenu()). A block is tracked while its menu is open and the chip's
    /// range is used only while the block is unchanged (PageController.trackedRow).
    property DateChipMenu dateMenu: null
    property int dateMenuBlock: -1
    /// What the open menu's choice applies to: "own" (a token of block dateMenuHandle's text),
    /// "in" (a copy of a block of another file: dateMenuPath, dateMenuLine, dateMenuRaw), "cell"
    /// (table cell dateMenuTableRow, dateMenuColumn of block dateMenuHandle); "" once chosen.
    property string dateMenuMode: ""
    property int dateMenuHandle: -1
    property string dateMenuPath
    property int dateMenuLine: -1
    property string dateMenuRaw
    property int dateMenuTableRow: -1
    property int dateMenuColumn: -1
    /// The mode a dialog opened from the menu ("Zmień…") applies its result in (tokenRun()).
    property string tokenRunMode: ""
    /// The chip the menu is for (PageController.dateChip / tokenChipIn / tableCellChip).
    MapHolder {
        id: dateMenuChip
        function kind(): string { return dateMenuChip.map.kind ? dateMenuChip.map.kind : "" }
        function rule(): string { return dateMenuChip.map.rule ? dateMenuChip.map.rule : "" }
        function priority(): string { return dateMenuChip.map.priority ? dateMenuChip.map.priority : "" }
        function at(): string { return dateMenuChip.map.at }
        /// The reminder dialog's time: dialogAt, else at.
        function dialogAt(): string { return dateMenuChip.map.dialogAt ? dateMenuChip.map.dialogAt : dateMenuChip.map.at }
        function context(): string { return dateMenuChip.map.context ? dateMenuChip.map.context : "" }
        /// The rule as the chip has it.
        function rawRule(): string { return dateMenuChip.map.rule }
    }
    function isDateChipLink(link: string): bool {
        return Js.startsWith(link, "date:") || Js.startsWith(link, "reminder:") || Js.startsWith(link, "recur:")
                || Js.startsWith(link, "prio:") || Js.startsWith(link, "day:")
    }
    /// Links whose click edits the text (their menu is the right button's): date, 🔁 and priority chips.
    function editsOnClick(link: string): bool {
        return Js.startsWith(link, "date:") || Js.startsWith(link, "recur:") || Js.startsWith(link, "prio:")
    }
    /// Open DateChipMenu for the chip in `chip` (PageController.dateChip / tokenChipIn /
    /// tableCellChip) at (x, y) of `item`; the choice goes to `mode` (dateMenuMode).
    function popupTokenMenu(chip: var, mode: string, item: Item, x: real, y: real): void {
        if (!dateMenu)
            dateMenu = dateMenuComponent.createObject(editor) as DateChipMenu
        menuAbove = null
        closeSuggestions()
        dateMenuChip.map = chip
        dateMenuMode = mode
        dateMenu.kind = dateMenuChip.kind()
        dateMenu.alarm = dateMenuChip.map.alarm === true
        dateMenu.rule = dateMenuChip.rule()
        dateMenu.priority = dateMenuChip.priority()
        dateMenu.logseq = dateMenuChip.map.logseq === true
        dateMenu.isTask = dateMenuChip.map.isTask === true
        dateMenu.occurrence = false
        // Above the docked toolbar and the keyboard (it flips above its item when needed).
        dateMenu.bottomMargin = menuBottomMargin()
        dateMenu.popup(item, x, y)
    }
    function chooseDateAction(action: string, a: var, b: var): void {
        tokenA = a
        tokenB = b
        const mode = dateMenuMode
        dateMenuMode = ""
        if (mode === "own")
            chooseOwnToken(dateMenuHandle, action)
        else if (mode !== "")
            chooseTokenWith(mode, action)
    }
    /// The values of the token action being applied (DateChipMenu.chosen's, the dialog's: `a`, `b`
    /// of PageController.applyTokenAction), kept here: compiled code passes no `var` argument on
    /// after other calls.
    property var tokenA
    property var tokenB
    /// The menu of a token in block `row`'s text. False when `link` names none.
    function openDateMenu(row: int, link: string, item: Item, x: real, y: real): bool {
        if (!controller || row < 0 || row >= controller.blocks.count || !isDateChipLink(link))
            return false
        chipFound.map = controller.dateChip(row, link)
        if (!chipFound.map.kind)
            return false
        if (dateMenuBlock >= 0)
            controller.untrack(dateMenuBlock)
        dateMenuBlock = controller.trackBlock(row)
        dateMenuHandle = dateMenuBlock
        popupTokenMenu(chipFound.map, "own", item, x, y)
        return true
    }
    /// A chip just asked for (before its menu shows it).
    MapHolder { id: chipFound }
    function chooseOwnToken(handle: int, action: string): void {
        if (!controller)
            return
        const row = controller.trackedRow(handle) + 0 // (a typed copy: passed on after other calls)
        controller.untrack(handle)
        if (dateMenuBlock === handle)
            dateMenuBlock = -1
        if (row < 0)
            return
        const kind = dateMenuChip.kind()
        switch (action) {
        case "change":
            if (kind === "reminder")
                dialogs.editReminder(row, dateMenuChip.map.from, dateMenuChip.map.to)
            else if (kind === "recurrence")
                dialogs.pickRecurrenceAt(row, dateMenuChip.map.from, dateMenuChip.map.to, dateMenuChip.map.rule)
            else
                dialogs.pickDateAt(row, dateMenuChip.map.from, dateMenuChip.map.to, kind, dateMenuChip.map.at)
            break
        case "blockRecurrenceChange": dialogs.pickRecurrence(row); break
        case "open": activateLink(row, dateMenuChip.map.url); break
        case "openSide": controller.activateLink(row, dateMenuChip.map.url, true); break
        case "copy": copyText(dateMenuChip.map.copyText ? dateMenuChip.map.copyText : ""); break
        default:
            controller.applyTokenAction(row, dateMenuChip.map.from, dateMenuChip.map.to, action, tokenA, tokenB)
            break
        }
    }
    /// The menu of a token in a copy of a block of `path` (a query result, an embed): `raw` is the
    /// block's Markdown as the index has it, `line` its line. The edit goes to that block's file
    /// (PageController.editTokenIn) with a toast to undo it; on this page it is an undo step.
    function openTokenMenuIn(path: string, line: int, raw: string, link: string, item: Item, x: real, y: real): bool {
        if (!controller || path === "" || line < 0 || raw === "" || !isDateChipLink(link))
            return false
        chipFound.map = controller.tokenChipIn(raw, link)
        if (!chipFound.map.kind)
            return false
        dateMenuPath = path
        dateMenuLine = line
        dateMenuRaw = raw
        popupTokenMenu(chipFound.map, "in", item, x, y)
        return true
    }
    /// The menu of a token in a table cell (tableRow -1 = the header): the cell's text changes
    /// (PageController.applyTableCellToken, one undo step).
    function openCellTokenMenu(row: int, tableRow: int, column: int, link: string, item: Item, x: real, y: real): bool {
        if (!controller || row < 0 || !isDateChipLink(link))
            return false
        chipFound.map = controller.tableCellChip(row, tableRow, column, link)
        if (!chipFound.map.kind)
            return false
        if (dateMenuBlock >= 0)
            controller.untrack(dateMenuBlock)
        dateMenuBlock = controller.trackBlock(row)
        dateMenuHandle = dateMenuBlock
        dateMenuTableRow = tableRow
        dateMenuColumn = column
        popupTokenMenu(chipFound.map, "cell", item, x, y)
        return true
    }
    /// A token menu's choice in `mode` ("in", "cell"), applied by tokenRun(); "change" opens the
    /// dialog that ends in it (EditorDialogs.applied with the target "token").
    function chooseTokenWith(mode: string, action: string): void {
        tokenRunMode = mode
        const kind = dateMenuChip.kind()
        switch (action) {
        case "change":
            if (kind === "reminder") {
                const at = dateMenuChip.dialogAt()
                const alarm = dateMenuChip.map.alarm === true
                const rule = dateMenuChip.rule()
                const context = dateMenuChip.context()
                dialogs.editReminderWith("token", at, alarm, rule, context)
            } else if (kind === "recurrence") {
                dialogs.pickRecurrenceWith("token", dateMenuChip.rawRule(), "recurrence")
            } else {
                const at = dateMenuChip.at()
                dialogs.pickDateWith("token", kind, at)
            }
            break
        case "blockRecurrenceChange":
            dialogs.pickRecurrenceWith("token", dateMenuChip.rawRule(), "blockRecurrence")
            break
        default: tokenRun(action); break
        }
    }
    /// Apply a token menu's (or its dialog's) choice in tokenRunMode, with tokenA and tokenB: to the
    /// token of a copy of another file's block (editTokenIn, the toast reportTokenEdit()) or to a
    /// table cell's.
    function tokenRun(action: string): void {
        if (!controller)
            return
        if (tokenRunMode === "in") {
            reportTokenEdit(controller.editTokenIn(dateMenuPath, dateMenuLine, dateMenuRaw, dateMenuChip.map.ref,
                                                   action, tokenA, tokenB), action)
        } else if (tokenRunMode === "cell") {
            const r = controller.trackedRow(dateMenuHandle)
            if (r >= 0)
                controller.applyTableCellToken(r, dateMenuTableRow, dateMenuColumn, dateMenuChip.map.from,
                                               dateMenuChip.map.to, action, tokenA, tokenB)
        }
    }
    Connections {
        target: dialogsItem
        function onApplied(target: string, action: string, a: var, b: var) {
            if (target !== "token")
                return
            editor.tokenA = a
            editor.tokenB = b
            editor.tokenRun(action)
        }
    }
    /// The toast after editTokenIn(): what changed, with "Undo" for another file; why not.
    function reportTokenEdit(r: var, action: string): void {
        if (!r)
            return
        tokenEdit.map = r
        if (tokenEdit.map.ok) {
            if (tokenEdit.undo() > 0) {
                const text = action === "recurrence" || action === "blockRecurrence" ? qsTr("Repeat changed")
                           : action === "priority" || action === "blockPriority" ? qsTr("Priority changed")
                           : action === "remove" ? qsTr("Removed")
                           : action === "alarm" || action === "reminder" ? qsTr("Reminder changed")
                           : qsTr("Date changed")
                const undo = toastAction("undoTokenEdit", qsTr("Undo"))
                undo.undo = tokenEdit.undo()
                notice(text, "success", undo)
            }
            return
        }
        const reason = tokenEdit.map.reason
        if (reason === "gone" || reason === "changed")
            notice(qsTr("The note changed in the meantime; nothing was changed."), "info", null)
        else if (reason === "failed")
            notice(qsTr("The change could not be saved."), "error", null)
    }
    /// PageController.editTokenIn's result.
    MapHolder {
        id: tokenEdit
        function undo(): int { return tokenEdit.map.undo ? tokenEdit.map.undo : 0 }
    }
    Component {
        id: dateMenuComponent
        DateChipMenu {
            onChosen: (action, a, b) => editor.chooseDateAction(action, a, b)
        }
    }

    // ---- The menu of a link: [[page]], #tag, ((block)), web address (LinkMenu) ------------------
    /// LinkMenu for a page:, tag:, block: or ext: link of rendered text — `fromPath`: the page the
    /// link is written on ("" = this one) —; its Otwórz items do what a click does (in the side
    /// panel with newPane): on the link in block `row` (requester null), else what `requester`
    /// does on linkMenuOpenRequested(requester, link, newPane) (a query result, an embed). Created
    /// when first opened. False when `link` is no such link.
    property LinkMenu linkMenu: null
    property int linkMenuRow: -1
    property Item linkMenuRequester: null
    property string linkMenuLink
    /// The menu is open for a link (its Otwórz items open it once).
    property bool linkMenuArmed: false
    signal linkMenuOpenRequested(Item requester, string link, bool newPane)
    /// What the menu is about (PageController.linkMenu).
    MapHolder {
        id: linkMenuInfo
        function tag(): string { return linkMenuInfo.map.tag }
    }
    function isLinkMenuLink(link: string): bool {
        return Js.startsWith(link, "page:") || Js.startsWith(link, "part:") || Js.startsWith(link, "tag:")
                || Js.startsWith(link, "block:") || Js.startsWith(link, "ext:")
    }
    function openLinkMenu(link: string, fromPath: string, row: int, requester: Item, item: Item, x: real, y: real): bool {
        if (!controller || !isLinkMenuLink(link))
            return false
        chipFound.map = controller.linkMenu(link, fromPath)
        if (!chipFound.map.kind)
            return false
        if (!linkMenu)
            linkMenu = linkMenuComponent.createObject(editor) as LinkMenu
        menuAbove = null
        closeSuggestions()
        linkMenuInfo.map = chipFound.map
        linkMenuRow = row
        linkMenuRequester = requester
        linkMenuLink = link
        linkMenuArmed = true
        linkMenu.kind = linkMenuInfo.map.kind
        linkMenu.unresolved = linkMenuInfo.map.unresolved === true
        linkMenu.folder = linkMenuInfo.map.target === "folder" // a path part naming a folder
        linkMenu.bottomMargin = menuBottomMargin()
        linkMenu.popup(item, x, y)
        return true
    }
    /// A middle click (a mouse's wheel button) on `link` in block `row`'s text: a page, tag or
    /// block link opens in a new tab (OpenIn; nothing outside the app's shell). True when it did.
    function openLinkInTab(row: int, link: string): bool {
        if (!controller || !OpenIn.tabs || !isLinkMenuLink(link) || Js.startsWith(link, "ext:"))
            return false
        OpenIn.begin("tab")
        activateLink(row, link)
        OpenIn.end()
        return true
    }
    /// The link menu of a link in block `row`'s text.
    function openBlockLinkMenu(row: int, link: string, item: Item, x: real, y: real): bool {
        return openLinkMenu(link, "", row, null, item, x, y)
    }
    /// What a click on the menu's link does (`newPane`: in the side panel).
    function openMenuLink(newPane: bool): void {
        if (linkMenuRequester !== null)
            linkMenuOpenRequested(linkMenuRequester, linkMenuLink, newPane)
        else if (newPane)
            controller.activateLink(linkMenuRow, linkMenuLink, true)
        else
            activateLink(linkMenuRow, linkMenuLink)
    }
    function chooseLinkAction(action: string): void {
        const open = linkMenuArmed
        linkMenuArmed = false
        if (!controller)
            return
        switch (action) {
        case "open": if (open) openMenuLink(false); break
        case "openSide": if (open) openMenuLink(true); break
        case "openTab":
            if (open) {
                OpenIn.begin("tab")
                openMenuLink(false)
                OpenIn.end()
            }
            break
        case "openWindow":
            if (open) {
                OpenIn.begin("window")
                openMenuLink(false)
                OpenIn.end()
            }
            break
        case "copy": copyText(linkMenuInfo.map.copyText ? linkMenuInfo.map.copyText : ""); break
        case "overview": controller.navigate({ view: "tag", tag: linkMenuInfo.tag(), overview: true }); break
        case "tasks":
            if (linkMenu)
                controller.navigate({ view: "tasks", query: linkMenu.tasksQuery(linkMenuInfo.tag()) })
            break
        }
    }
    /// Copy `text` and say so ("Skopiowano …", or that the clipboard is not available).
    function copyText(text: string): bool {
        if (!controller || text === "")
            return false
        copied.map = controller.copyText(text)
        const ok = copied.map.ok === true
        if (ok)
            notice(qsTr("Copied %1").arg(copied.map.text), "success", null)
        else
            notice(copyFailure(copied.reason()), "error", null)
        return ok
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
    function openTaskStatusMenu(row: int, item: Item, x: real, y: real): void {
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
        // The timer and "Dodaj zależność…" for a task (docs/SYNTAX.md §5).
        taskMenu.showTaskTools = taskMenu.status !== ""
        if (taskMenu.showTaskTools) {
            taskInfo.map = controller.taskInfo(row)
            taskMenu.clockRunning = taskInfo.map.clockRunning === true
        } else {
            taskMenu.clockRunning = false
        }
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
    function canHaveTaskStatus(row: int): bool {
        if (!controller || row < 0 || row >= controller.blocks.count)
            return false
        const kind = controller.blocks.kindAt(row)
        return kind === "listItem" || kind === "paragraph"
    }
    /// Ctrl+Shift+Enter: the menu under the caret of the block being edited, else under the
    /// selected block's first line; `selection`: for every block of the selected range (`row` is
    /// its focus end).
    function openTaskStatusMenuFor(row: int, selection: bool): void {
        if (!canHaveTaskStatus(row) && !selection)
            return
        const edit = activeEditor
        const item = list.itemAtIndex(row) as BlockDelegate
        if (edit && edit.row === row && edit.active) {
            openTaskStatusMenu(row, edit, edit.caretRect.x, edit.caretRect.y + edit.caretRect.height)
        } else if (item) {
            openTaskStatusMenu(row, item, item.contentLeft, item.firstLineCenter + lineHeight / 2)
        } else {
            openTaskStatusMenu(row, null, 0, 0)
        }
        taskMenuForSelection = selection
        if (taskMenu)
            taskMenu.highlightStatus()
    }
    /// The touch toolbar's task button held down: the menu right above the button (the toolbar
    /// is docked on the keyboard).
    function openTaskStatusMenuAbove(row: int, item: Item): void {
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
    function followToolbarWith(menu: BMenu, anchor: Item, y: real): void {
        menuAbove = menu && menu.visible ? menu : null
        menuAboveAnchor = anchor
        menuAboveY = y
    }
    function followToolbar(): void {
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
        function onOverlapChanged() { followLater.restart() }
    }
    Connections {
        target: editor.dockHost
        enabled: editor.menuAbove !== null
        function onHeightChanged() { followLater.restart() }
    }
    Later {
        id: followLater
        onTriggered: if (editor.alive) editor.followToolbar()
    }
    /// The status of a block's task as the menu shows it ("" = none).
    function taskStatusOf(row: int): string {
        return controller.blocks.kindAt(row) === "listItem" ? controller.blocks.taskStatusAt(row) : ""
    }
    function chooseTaskStatus(status: string): void {
        if (!controller || taskMenuBlock < 0)
            return
        const row = controller.trackedRow(taskMenuBlock) + 0 // (a typed copy: passed on after other calls)
        controller.untrack(taskMenuBlock)
        taskMenuBlock = -1
        if (taskMenuForSelection) {
            taskMenuForSelection = false
            controller.setSelectionTaskStatus(status) // one undo step; what already has it stays
        } else if (row >= 0 && status !== taskStatusOf(row)) {
            controller.setTaskStatus(row, status)
        }
    }
    /// The task the menu was opened for (its tracking ends), -1 when gone.
    function takeTaskMenuRow(): int {
        if (!controller || taskMenuBlock < 0)
            return -1
        const row = controller.trackedRow(taskMenuBlock)
        controller.untrack(taskMenuBlock)
        taskMenuBlock = -1
        taskMenuForSelection = false
        return row
    }
    /// Start or stop the timer of the task at `row` (one timer at a time: another one stops).
    function toggleTaskClock(row: int): bool {
        if (!controller || row < 0 || taskStatusOf(row) === "")
            return false
        taskInfo.map = controller.taskInfo(row)
        const running = taskInfo.map.clockRunning === true
        if (!controller.toggleTaskClock(row))
            return false
        notice(running ? qsTr("Timer stopped") : qsTr("Timer started"), "success", null)
        return true
    }
    MapHolder { id: taskInfo }
    /// The block the palette's task commands act on: the one being edited, else the selected one.
    function currentTaskRow(): int {
        const row = currentRow()
        return row >= 0 && taskStatusOf(row) !== "" ? row : -1
    }
    /// The palette's "Licznik czasu zadania": the current task's timer.
    function toggleCurrentTaskClock(): void {
        const row = currentTaskRow()
        if (row < 0) {
            notice(qsTr("Put the cursor in a task first"), "info", null)
            return
        }
        toggleTaskClock(row)
    }
    /// "Dodaj zależność…": the picker for the task at `row`.
    property TaskDependencyDialog dependencyDialog: null
    property int dependencyBlock: -1
    function openDependencyPicker(row: int): void {
        if (!controller || row < 0 || taskStatusOf(row) === "")
            return
        if (!dependencyDialog)
            dependencyDialog = dependencyDialogComponent.createObject(editor.Window.contentItem || editor) as TaskDependencyDialog
        if (dependencyBlock >= 0)
            controller.untrack(dependencyBlock)
        dependencyBlock = controller.trackBlock(row)
        dependencyDialog.open()
    }
    function openCurrentDependencyPicker(): void {
        const row = currentTaskRow()
        if (row < 0) {
            notice(qsTr("Put the cursor in a task first"), "info", null)
            return
        }
        openDependencyPicker(row)
    }
    Component {
        id: dependencyDialogComponent
        TaskDependencyDialog {
            id: dependencies
            onSearchRequested: (text) => {
                const row = editor.controller && editor.dependencyBlock >= 0
                          ? editor.controller.trackedRow(editor.dependencyBlock) : -1
                dependencies.items = row >= 0 ? editor.controller.dependencyCandidates(row, text) : []
            }
            onChosen: (path, line) => {
                const row = editor.controller.trackedRow(editor.dependencyBlock)
                if (row >= 0 && editor.controller.addTaskDependency(row, path, line))
                    editor.notice(qsTr("Dependency added"), "success", null)
            }
            onClosed: {
                if (editor.controller && editor.dependencyBlock >= 0)
                    editor.controller.untrack(editor.dependencyBlock)
                editor.dependencyBlock = -1
            }
        }
    }
    Component {
        id: taskMenuComponent
        TaskStatusMenu {
            onStatusChosen: (status) => editor.chooseTaskStatus(status)
            onClockRequested: editor.toggleTaskClock(editor.takeTaskMenuRow())
            onDependencyRequested: editor.openDependencyPicker(editor.takeTaskMenuRow())
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
        id: dialogsItem
        editorItem: editor
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
            editorItem: editor
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
        // Centred on the dock host (its parent; the parent itself while it is set: a binding to an
        // anchor of `undefined` is not compiled).
        x: parent ? (parent.width - width) / 2 : 0
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
                    editorItem: editor
                }
            }
        }
    }

    // ---- The edited text's menu (desktop right-click) -------------------------------------------------
    /// Wytnij, Kopiuj, Wklej, Zaznacz wszystko for the text of the block being edited: a right-click
    /// in it with a mouse, touchpad or stylus (a bare TextEdit has no menu of its own; touch UIs have
    /// the system's selection handles and popup). On a word marked as misspelled (SpellCheck,
    /// docs/UI.md "Spell checking") its corrections come first, then Dodaj do słownika and Ignoruj;
    /// on touch UIs a tap on such a word opens only those (openSpellingMenu). Made when first opened.
    property BMenu textMenu: null
    property InlineEditor textMenuEdit: null
    /// The misspelled word the menu is about ({from, to, word, text}; {}: none).
    MapHolder {
        id: textMenuSpelling
        /// The word as checked ("" when the menu is about none).
        function word(): string { return textMenuSpelling.map.word === undefined ? "" : textMenuSpelling.map.word }
        function text(): string { return textMenuSpelling.map.text ? textMenuSpelling.map.text : "" }
    }
    /// Only the spelling's part (touch UIs).
    property bool textMenuSpellingOnly: false
    function openTextMenu(input: TextEdit, x: real, y: real): void {
        // (A TextEdit: an InlineEditor passes itself, which compiled code cannot do as its own type.)
        const edit = input as InlineEditor
        if (!edit || !controller)
            return
        // A click outside the selected text moves the caret there first.
        const pos = edit.positionAt(x, y) + 0 // (typed copies: TextEdit's own cannot be compared)
        const start = edit.selectionStart + 0
        const end = edit.selectionEnd + 0
        if (pos < start || pos > end || start === end)
            edit.cursorPosition = pos
        chipFound.map = edit.misspelledAt(pos)
        showTextMenu(edit, chipFound.map, false, x, y)
    }
    /// The corrections of the misspelled word at `pos` of the edited text (touch UIs: a tap on it).
    function openSpellingMenu(input: TextEdit, pos: int, x: real, y: real): void {
        // (A TextEdit: an InlineEditor passes itself, which compiled code cannot do as its own type.)
        const edit = input as InlineEditor
        if (!edit || !controller)
            return
        chipFound.map = edit.misspelledAt(pos)
        if (chipFound.map.word !== undefined)
            showTextMenu(edit, chipFound.map, true, x, y)
    }
    /// SpellCheck's corrections of `word` (none until asked).
    function suggestionsOf(word: string): list<string> {
        return SpellCheck.suggestionsFor(word)
    }
    readonly property list<string> noStrings: []
    function showTextMenu(edit: InlineEditor, spelling: var, spellingOnly: bool, x: real, y: real): void {
        if (!textMenu)
            textMenu = textMenuComponent.createObject(editor) as BMenu
        textMenuEdit = edit
        textMenuSpelling.map = spelling
        const word = textMenuSpelling.word()
        textMenuSpellingOnly = spellingOnly && word !== ""
        // Asked once per word (the answer is kept: SpellCheck.suggestionsFor).
        if (word !== "" && suggestionsOf(word).length === 0)
            SpellCheck.requestSuggestions(word)
        textMenu.bottomMargin = menuBottomMargin()
        textMenu.popup(edit, x, y)
    }
    Component {
        id: textMenuComponent
        BMenu {
            id: menu
            objectName: "editTextMenu"
            readonly property InlineEditor edit: editor.textMenuEdit
            readonly property bool hasSelection: edit !== null && editor.differs(edit.selectionStart, edit.selectionEnd)
            /// The misspelled word ("" when none).
            readonly property string word: textMenuSpelling.word()
            readonly property bool misspelled: word !== ""
            readonly property bool textItems: !editor.textMenuSpellingOnly
            /// The corrections of `word`; `suggesting` until they come.
            property list<string> suggestions
            property bool suggesting: false
            readonly property list<string> shownSuggestions: misspelled ? suggestions : editor.noStrings
            Connections {
                target: textMenuSpelling
                function onMapChanged() {
                    menu.suggestions = menu.misspelled ? editor.suggestionsOf(menu.word) : editor.noStrings
                    menu.suggesting = menu.misspelled && menu.suggestions.length === 0
                }
            }
            Connections {
                target: SpellCheck
                function onSuggestionsReady(word: string, list: list<string>) {
                    if (menu.misspelled && word === menu.word) {
                        menu.suggestions = list
                        menu.suggesting = false
                    }
                }
            }
            function replaceWith(text: string): void {
                if (edit && menu.misspelled)
                    edit.replaceWord(textMenuSpelling.map.from, textMenuSpelling.map.to, text)
            }

            // The spelling: corrections, then the dictionary.
            Instantiator {
                model: menu.shownSuggestions
                delegate: BMenuItem {
                    required property string modelData
                    objectName: "spellSuggestion"
                    text: modelData
                    font.weight: Font.DemiBold
                    onTriggered: menu.replaceWith(modelData)
                }
                onObjectAdded: (index, object) => menu.insertItem(index, object as Item)
                onObjectRemoved: (index, object) => menu.removeItem(object as Item)
            }
            BMenuItem {
                objectName: "spellNoSuggestions"
                visible: menu.misspelled && menu.suggestions.length === 0
                height: visible ? implicitHeight : 0
                enabled: false
                text: menu.suggesting ? qsTr("Looking for corrections…") : qsTr("No corrections")
            }
            BMenuItem {
                objectName: "spellAddWord"
                visible: menu.misspelled
                height: visible ? implicitHeight : 0
                //: %1: the word, added to the personal dictionary of spell checking
                text: qsTr("Add “%1” to dictionary").arg(textMenuSpelling.text())
                iconName: "plus"
                onTriggered: SpellCheck.addToDictionary(menu.word)
            }
            BMenuItem {
                objectName: "spellIgnore"
                visible: menu.misspelled
                height: visible ? implicitHeight : 0
                text: qsTr("Ignore", "spelling")
                iconName: "eye-off"
                onTriggered: SpellCheck.ignore(menu.word)
            }
            BMenuSeparator {
                visible: menu.misspelled && menu.textItems
                height: visible ? implicitHeight : 0
            }
            BMenuItem {
                objectName: "editTextCut"
                visible: menu.textItems
                height: visible ? implicitHeight : 0
                text: qsTr("Cut")
                iconName: "scissors"
                shortcut: "Ctrl+X"
                enabled: menu.hasSelection
                onTriggered: editor.copyEditSelection(menu.edit, true)
            }
            BMenuItem {
                objectName: "editTextCopy"
                visible: menu.textItems
                height: visible ? implicitHeight : 0
                text: qsTr("Copy")
                iconName: "copy"
                shortcut: "Ctrl+C"
                enabled: menu.hasSelection
                onTriggered: editor.copyEditSelection(menu.edit, false)
            }
            BMenuItem {
                objectName: "editTextPaste"
                // Not in the browser: a page reads the clipboard only from a paste event (Ctrl+V).
                visible: menu.textItems && DeviceInfo.platform !== "wasm"
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
            BMenuSeparator {
                visible: menu.textItems
                height: visible ? implicitHeight : 0
            }
            BMenuItem {
                objectName: "editTextHighlight"
                visible: menu.textItems
                height: visible ? implicitHeight : 0
                text: qsTr("Highlight…")
                iconName: "highlighter"
                onTriggered: {
                    if (menu.edit) {
                        menuLater.edit = menu.edit
                        menuLater.highlight = true
                        menuLater.restart()
                    }
                }
            }
            BMenuItem {
                objectName: "editTextSelectAll"
                visible: menu.textItems
                height: visible ? implicitHeight : 0
                text: qsTr("Select all")
                shortcut: "Ctrl+A"
                onTriggered: if (menu.edit) menu.edit.selectAll()
            }
            BMenuItem {
                objectName: "editTextBlockMenu"
                visible: menu.textItems
                height: visible ? implicitHeight : 0
                text: qsTr("Block…")
                iconName: "ellipsis"
                onTriggered: {
                    if (menu.edit) {
                        menuLater.edit = menu.edit
                        menuLater.highlight = false
                        menuLater.restart()
                    }
                }
            }
            // The keyboard goes back to the edited text (its selection stayed: persistentSelection).
            onClosed: if (edit && edit.active) edit.ensureFocus()
        }
    }

    // ---- Coloured highlights (docs/UI.md "Coloured highlights") ---------------------------------
    /// The colours of "Zakreśl": the edited text's menu ("Zakreśl…"), the touch toolbar's button.
    /// A colour highlights the selection (or recolours the highlight around it; `==…==` for the
    /// default colour, Highlightr's `<mark style="background: …;">` for the others), "Usuń
    /// zakreślenie" removes the highlight around the caret — PageController.setHighlight, one undo
    /// step; the highlighted text stays selected. Made when first opened.
    property BMenu highlightMenu: null
    property InlineEditor highlightMenuEdit: null
    /// The text menu's "Zakreśl…" and "Blok…" open their menus once it is closed: at the caret of
    /// `edit` (`highlight`: the colours, else the block menu).
    Later {
        id: menuLater
        property InlineEditor edit: null
        property bool highlight: false
        onTriggered: {
            const edit = menuLater.edit
            if (!editor.alive || !edit)
                return
            const bottom = edit.caretRect.y + edit.caretRect.height
            if (menuLater.highlight)
                editor.openHighlightMenu(edit, edit, edit.caretRect.x, bottom)
            else
                editor.openBlockMenu(edit.row, edit, 0, bottom)
        }
    }
    /// `a` !== `b` for two values that are only passed on here (shadowable ones cannot be compared).
    function differs(a: int, b: int): bool {
        return a !== b
    }
    /// What a palette colour (TextHelper.highlightColors) is called in the menu.
    function highlightColorLabel(name: string): string {
        switch (name) {
        case "default": return qsTr("Default", "highlight colour")
        case "red": return qsTr("Red")
        case "orange": return qsTr("Orange")
        case "yellow": return qsTr("Yellow")
        case "green": return qsTr("Green")
        case "cyan": return qsTr("Cyan")
        case "blue": return qsTr("Blue")
        case "purple": return qsTr("Purple")
        case "pink": return qsTr("Pink")
        case "grey": return qsTr("Grey")
        }
        return name
    }
    /// Highlight the selection of the edited text `edit` in `color` (a palette name; "" removes).
    function applyHighlight(edit: InlineEditor, color: string): void {
        if (!edit || !controller || !edit.active)
            return
        EditorInput.commitPreedit(edit)
        highlighted.map = controller.setHighlight(edit.row, edit.selectionStart, edit.selectionEnd, color)
        if (!highlighted.map.changed) {
            if (color === "")
                notice(qsTr("No highlight here"), "info", null)
            else if (highlighted.map.refused)
                notice(qsTr("Select text within one line, not across a highlight's edge"), "info", null)
            return
        }
        highlightLater.edit = edit
        highlightLater.from = highlighted.map.from
        highlightLater.to = highlighted.map.to
        highlightLater.restart()
    }
    /// PageController.setHighlight's result.
    MapHolder { id: highlighted }
    /// The highlighted text selected again once the edit has settled.
    Later {
        id: highlightLater
        property InlineEditor edit: null
        property int from: 0
        property int to: 0
        onTriggered: {
            const edit = highlightLater.edit
            if (editor.alive && edit && edit.active) {
                edit.ensureFocus()
                edit.select(highlightLater.from, highlightLater.to)
            }
        }
    }
    function openHighlightMenu(edit: InlineEditor, item: Item, x: real, y: real): void {
        if (!edit || !controller)
            return
        if (!highlightMenu)
            highlightMenu = highlightMenuComponent.createObject(editor) as BMenu
        highlightMenuEdit = edit
        highlightMenu.bottomMargin = menuBottomMargin()
        highlightMenu.popup(item, x, y)
    }
    /// The touch toolbar's "Zakreśl": the colours above the bar.
    function openHighlightMenuAbove(edit: InlineEditor, item: Item): void {
        if (!edit || !controller)
            return
        if (!highlightMenu)
            highlightMenu = highlightMenuComponent.createObject(editor) as BMenu
        const y = -highlightMenu.implicitHeight - Theme.space1
        openHighlightMenu(edit, item, 0, y)
        followToolbarWith(highlightMenu, item, y)
    }
    Component {
        id: highlightMenuComponent
        BMenu {
            id: colours
            objectName: "highlightMenu"
            Instantiator {
                model: TextHelper.highlightColors()
                delegate: BMenuItem {
                    required property string name
                    required property color tint
                    objectName: "highlight-" + name
                    text: editor.highlightColorLabel(name)
                    swatch: name === "default" ? Theme.mark : tint
                    onTriggered: editor.applyHighlight(editor.highlightMenuEdit, name)
                }
                onObjectAdded: (index, object) => colours.insertItem(index, object as Item)
                onObjectRemoved: (index, object) => colours.removeItem(object as Item)
            }
            BMenuSeparator {}
            BMenuItem {
                objectName: "highlight-remove"
                text: qsTr("Remove highlight")
                iconName: "eraser"
                onTriggered: editor.applyHighlight(editor.highlightMenuEdit, "")
            }
            // The keyboard goes back to the edited text (its selection stayed: persistentSelection).
            onClosed: if (editor.highlightMenuEdit && editor.highlightMenuEdit.active) editor.highlightMenuEdit.ensureFocus()
        }
    }

    /// The dialogs (for tests and the playground's scripted states). (final: looked into through
    /// `editor` by the touch toolbar and the views.)
    final property EditorDialogs dialogs: dialogsItem
    final property ListView listView: list
}
