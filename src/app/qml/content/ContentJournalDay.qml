pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// ContentJournalDay — one day of JournalsView: the date as a heading ("Wednesday, October 15,
// 2026", a click opens the day as a page), "Open as page", and the day's note in an
// embedded block editor (its own PageController; the editor's list does not scroll — the
// journal list does — and is as tall as its blocks). A day without a note yet (today) shows a
// prompt; the first edit creates the file — from the vault's journal template when there is one.
//
//   ContentJournalDay { date: model.date; path: model.path; exists: model.exists;
//                       isToday: model.isToday; scroller: journals; toolbarHost: journals;
//                       stream: stream }
//
// Caret visibility is the journal list's job (scroller.ensureVisible(item, rect)): the embedded
// editor asks it instead of its own list (BlockEditor.scroller). The touch toolbar docks on
// `toolbarHost` — the journal stream's frame, which does not scroll —, so it stays on the keyboard
// while the list moves, and the day does not grow for it.
// Ctrl+E (Markdown source) opens the day as a page in source mode instead of a source editor
// inside the list. Fold state is kept per day like in PageView. Each day is shown in its own note
// mode (its `brulion:` key, else the notebook's journal mode — outline by default): a day switched
// to a document reads like one in the stream too, on the outline days' left edge (its editor's
// documentInset) unless the journal is a document by default. Blocks may be selected across
// several days at once (`stream`, StreamSelection: copying only).
// With BRULION_JOURNAL_DAY_ITEM=1 (EditorInput.journalDayItem) a day that is not being edited is
// drawn by one JournalDayItem instead of its editor's rows (docs/UI.md "A journal day as one item"):
// the editor (its controller, geometry, functions) is there, its list makes no rows (`drawn`). A
// tap, a click, a long press, a selection, editing, a zoom, the day becoming the stream's active
// one — anything that needs the rows — makes them (materialize(): the same heights, so nothing
// moves) and the input goes on to the editor's own functions; the day keeps its rows from then on.
FocusScope {
    id: day

    required property string date
    required property string path
    required property bool exists
    required property bool isToday
    /// The journal list: ensureVisible(item, rect) and scrollBy(dy).
    final property EditorScroller scroller: null
    /// Where the editor's touch toolbar docks (BlockEditor.toolbarHost); null: the day's editor.
    final property Item toolbarHost: null
    /// The stream's selection over several days (JournalsView): the day's controller is attached
    /// to it while the day exists.
    final property StreamSelection stream: null
    /// The stream's find bar (JournalsView): every day's editor tints its matches while it is open.
    final property NoteFind find: null
    /// The search over the stream's days (JournalsView): the day's controller is attached to it
    /// while the day exists (its live text is searched instead of its file).
    final property StreamFind streamFind: null
    /// This day shows the tools of a selection over several days (it becomes the active day).
    readonly property bool spanTools: stream !== null && stream.active && stream.toolsController === page
    // (Also a day created again while scrolling: the keys, Back and the selection mode come here.)
    onSpanToolsChanged: {
        if (day.spanTools) {
            day.materialize()
            day.activated()
            dayEditor.spanFocusHere(day.stream.focusRow, false)
        }
    }
    property bool compactHeader: Theme.compact

    /// This day's editor got the caret (JournalsView stops editing in the others).
    signal activated()

    final property PageController controller: page
    final property BlockEditor editor: dayEditor
    readonly property bool editing: page.editingRow >= 0
    /// Nothing to show (also a day holding nothing but its note mode).
    readonly property bool empty: page.loaded && page.blocks.visibleCount === 0
    property bool _foldsRestored: false
    /// The editor's last message with an action (a toast's button runs it: messageToast).
    property EditorToastAction messageAction: null
    property int messageToast: -1
    Connections {
        target: AppController
        function onToastActionTriggered(id: int) {
            if (id === day.messageToast && day.messageAction)
                day.messageAction.run()
        }
    }

    // The day is laid out with plain bindings, no Layouts: the journal list positions a day with
    // the height it has when it is created, and a layout's size only arrives with the next polish
    // — a day growing after the list had placed it pushed every day below it down while the list
    // scrolled up (docs/UI.md "Scrolling performance"). Its editor builds all its blocks at once
    // for the same reason (BlockEditor.autoHeight).
    implicitHeight: dayEditor.y + dayEditor.height
    /// How far the editor's tap strip under the blocks is taller than the space it shows: on touch
    /// UIs a touch target instead of 12 px. The journal stream lets it reach into the gap before
    /// the next day by that much, so the days keep their spacing (0 while the day shows no blocks).
    readonly property real footerOverhang: dayEditor.height > 0 ? dayEditor.bottomSpace - Theme.space3 : 0

    Accessible.role: Accessible.Grouping
    Accessible.name: DatesHelper.formatLong(day.date)

    /// Start writing in a day: a day without a note starts from the journal template (when the
    /// vault has one), the caret in its first empty block. Like a day opened on its own (PageView),
    /// the note is created by the first change, never by starting to write (startNewPage).
    function begin(): void {
        // (Reading mode: a toast offers "Edit" instead.)
        if (!page.loaded || dayEditor.readOnlyRefuses())
            return
        day.materialize()
        if (page.blocks.visibleCount > 0) {
            dayEditor.startEditing(page.lastVisibleRow(), -1)
            return
        }
        dayEditor.startEditing(page.startNewPage(page.exists ? "" : AppController.routeText(Workspace.config, "journalTemplate")), -1)
    }
    function openAsPage(source: bool): void {
        page.save()
        // (`if`, not `cond ? {…} : {…}`: that compiles to undefined — docs/UI.md "Compiled QML".)
        if (source)
            AppController.navigate({ view: "journal", date: day.date, source: true })
        else
            AppController.navigate({ view: "journal", date: day.date })
    }
    function keepCaretVisible(): void {
        dayEditor.ensureEditVisible()
    }
    property list<var> _folds: []
    function restoreFolds(): void {
        if (day._foldsRestored || !page.loaded)
            return
        day._folds = Workspace.foldState(page.path)
        if (day._folds.length > 0)
            page.setFoldState(day._folds)
        day._foldsRestored = true
    }
    // Deferred like the editor's own calls (Later): gone with the day (a stream scrolled away).
    Later {
        id: revealLater
        onTriggered: if (dayEditor.alive) dayEditor.ensureEditVisible()
    }

    PageController {
        id: page
        objectName: "journalDayController"
        host: Workspace
        // The hidden Journal's days: index changes wait until the stream is shown again.
        active: day.visible
        path: day.path
        allowNewBlockIds: !AppController.inDocsVault // the documentation's pages stay as shipped
        readOnly: AppSettings.readingMode // docs/UI.md "Reading mode"
        onLoadingChanged: if (page.loaded && !page.loading) day.restoreFolds()
        onFoldStateChanged: {
            if (day._foldsRestored && page.loaded && !page.loading)
                Workspace.setFoldState(page.path, page.foldState)
        }
        // A block selection in this day: the other days' selections end (one at a time) — not the
        // part of a selection over several days the stream gives it.
        onSelectionChanged: {
            // (Selected blocks show on the editor's rows: a band each.)
            if (page.selectionCount > 0)
                day.materialize()
            if (page.selectionCount > 0 && !(day.stream && day.stream.applying))
                day.activated()
        }
        // A zoom into one of its blocks makes the day the active one: Back zooms it out first
        // (JournalsView.stepBack()).
        onZoomChanged: {
            if (page.zoomBlock >= 0) {
                day.materialize()
                day.activated()
            }
        }
        onConflictChanged: if (page.conflict) day.materialize()
        onEditingRowChanged: {
            if (page.editingRow >= 0) {
                day.materialize()
                day.activated()
                // Deferred, checking the editor is still alive: the day may be gone by then (the
                // stream left right after a block started being edited — a palette command, a link).
                revealLater.restart()
            }
        }
        // The list is no place for a whole-file editor: the day opens as a page in source mode.
        onSourceModeChanged: {
            if (page.sourceMode) {
                page.sourceMode = false
                day.openAsPage(true)
            }
        }
        onExternallyChanged: (merged) => {
            if (merged)
                AppController.toast(qsTr("Merged changes made elsewhere into this day"), "info")
        }
    }
    Component.onCompleted: {
        day.restoreFolds()
        if (day.stream)
            day.stream.attach(page)
        if (day.streamFind)
            day.streamFind.attach(page)
    }
    // The editor's delegates go first, while the editor still exists (they read its geometry).
    Component.onDestruction: {
        if (day.stream)
            day.stream.detach(page)
        if (day.streamFind)
            day.streamFind.detach(page)
        dayEditor.controller = null
    }

    // The end of a selection over several days moved into this day: it is the active one, its
    // editor has the keyboard (and on touch UIs the selection mode); the keys scroll to it.
    Connections {
        target: day.stream
        function onFocusMoved(controller: PageController, row: int, reveal: bool) {
            if (controller !== page)
                return
            day.materialize()
            day.activated()
            day.editor.spanFocusHere(row, reveal)
        }
    }

    Connections {
        target: Workspace
        function onPathRenamed(oldPath: string, newPath: string) {
            if (page.path === oldPath)
                page.followRename(oldPath, newPath)
        }
        // The notebook's journal mode changed (settings, another device).
        function onConfigChanged() { page.refreshMode() }
    }

    // ---- The date -------------------------------------------------------------------------------
    // Wide: "Wednesday, October 14, 2026" with a "Today" badge. Phones: the weekday (and "Today")
    // as a small line above "October 14, 2026", so the date fits on one line.
    Item {
        id: headerRow
        x: dayEditor.contentX + dayEditor.gutter
        width: Math.max(0, day.width - x - Math.max(Theme.space2, dayEditor.width - dayEditor.contentX - dayEditor.contentWidth))
        height: Math.max(dateColumn.height, openButton.visible ? openButton.implicitHeight : 0,
                         openIcon.visible ? openIcon.implicitHeight : 0)

        readonly property string longDate: DatesHelper.formatLong(day.date)
        readonly property int comma: Js.indexOf(longDate, ", ")
        readonly property string weekday: comma > 0 ? Js.slice(longDate, 0, comma) : ""
        readonly property string restOfDate: comma > 0 ? Js.sliceFrom(longDate, comma + 2) : longDate
        readonly property string relative: day.isToday ? qsTr("Today")
                                            : day.date === DatesHelper.addDays(DatesHelper.today, -1) ? qsTr("Yesterday") : ""
        /// The badge and the button at the right end.
        readonly property real actionsWidth: (badge.visible ? badge.implicitWidth + Theme.space2 : 0)
                                             + (openButton.visible ? openButton.implicitWidth : 0)
                                             + (openIcon.visible ? openIcon.implicitWidth : 0)

        Item {
            id: dateColumn
            y: (headerRow.height - height) / 2
            width: Math.max(0, headerRow.width - headerRow.actionsWidth - (headerRow.actionsWidth > 0 ? Theme.space2 : 0))
            height: dateText.y + dateText.height
            BText {
                id: weekdayLine
                visible: day.compactHeader
                width: parent.width
                text: (headerRow.relative !== "" ? headerRow.relative + " · " : "") + headerRow.weekday
                variant: "caption"
                font.weight: Theme.weightSemiBold
                font.capitalization: Font.AllUppercase
                font.letterSpacing: 0.6
                color: day.isToday ? Theme.accentText : Theme.fgMuted
                elide: Text.ElideRight
            }
            Text {
                id: dateText
                y: weekdayLine.visible ? weekdayLine.implicitHeight : 0
                width: parent.width
                text: day.compactHeader ? headerRow.restOfDate : headerRow.longDate
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                font.family: Theme.fontEditor
                font.pixelSize: day.compactHeader ? Theme.textXl
                                                  : (day.isToday ? Theme.text3xl : Theme.text2xl)
                font.weight: Theme.weightSemiBold
                font.underline: dateHover.hovered
                lineHeightMode: Text.FixedHeight
                lineHeight: Theme.lineHeightPx(font.pixelSize, Theme.lineHeightTight)
                color: Theme.fg
                Accessible.role: Accessible.Heading
                Accessible.name: headerRow.longDate
                // The tap area: a touch target tall on touch UIs, centred on the date's line(s).
                Item {
                    width: parent.width
                    height: Math.max(parent.height, Theme.touchUi ? Theme.touchTarget : 0)
                    y: (parent.height - height) / 2
                    HoverHandler { id: dateHover; enabled: !Theme.touchUi; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: day.openAsPage(false) }
                }
            }
        }
        BBadge {
            id: badge
            visible: !day.compactHeader && headerRow.relative !== ""
            x: dateColumn.width + Theme.space2
            y: (headerRow.height - height) / 2
            text: headerRow.relative
            variant: day.isToday ? "accent" : "neutral"
            solid: day.isToday
        }
        BButton {
            id: openButton
            visible: !day.compactHeader
            anchors.right: parent.right
            y: (headerRow.height - height) / 2
            size: "small"
            variant: "ghost"
            iconName: "external-link"
            text: qsTr("Open as page")
            onClicked: day.openAsPage(false)
        }
        BIconButton {
            id: openIcon
            visible: day.compactHeader
            anchors.right: parent.right
            y: (headerRow.height - height) / 2
            size: "small"
            iconName: "external-link"
            label: qsTr("Open as page")
            onClicked: day.openAsPage(false)
        }
    }

    // ---- Nothing written yet ------------------------------------------------------------------------
    Loader {
        id: emptyPrompt
        active: day.empty
        y: headerRow.height + Theme.space2
        width: day.width
        sourceComponent: Item {
            implicitHeight: Math.max(Theme.touchTarget, prompt.implicitHeight + 2 * Theme.space2)

            Rectangle {
                x: dayEditor.contentX + dayEditor.gutter - Theme.space2
                width: Math.min(dayEditor.contentWidth - dayEditor.gutter + 2 * Theme.space2, parent.width - x - Theme.space2)
                height: parent.height
                radius: Theme.radius
                color: promptTap.pressed ? Theme.bgPressed : promptHover.hovered || prompt.activeFocus ? Theme.bgHover : "transparent"
                border.width: 1
                border.color: Theme.borderSubtle

                BIcon {
                    id: promptIcon
                    x: Theme.space3
                    anchors.verticalCenter: parent.verticalCenter
                    name: "square-pen"
                    size: Theme.iconSize
                    color: Theme.fgMuted
                }
                BText {
                    id: prompt
                    x: promptIcon.x + promptIcon.width + Theme.space2
                    width: Math.max(0, parent.width - x - Theme.space3)
                    anchors.verticalCenter: parent.verticalCenter
                    text: dayEditor.readOnly ? (day.isToday ? qsTr("Nothing written today yet.")
                                                            : qsTr("Nothing written on this day."))
                          : day.isToday ? qsTr("What's on today? Click to start writing.")
                          : qsTr("Nothing written on this day. Click to start writing.")
                    variant: "muted"
                    wrapMode: Text.Wrap
                    activeFocusOnTab: true
                    Accessible.role: Accessible.Button
                    Accessible.name: text
                    Accessible.onPressAction: day.begin()
                    Keys.onReturnPressed: day.begin()
                    Keys.onSpacePressed: day.begin()
                }
                HoverHandler { id: promptHover; enabled: !Theme.touchUi; cursorShape: dayEditor.readOnly ? Qt.ArrowCursor : Qt.IBeamCursor }
                TapHandler { id: promptTap; onTapped: day.begin() }
                BFocusRing { visible: prompt.activeFocus; targetRadius: Theme.radius }
            }
        }
    }

    // ---- The day's blocks -----------------------------------------------------------------------
    // The setting again whenever it changes (the panel's header writes it through
    // setPropertiesExpanded(), which replaces a plain binding).
    Binding {
        target: dayEditor
        property: "propertiesExpanded"
        value: AppSettings.propertiesExpanded
    }
    BlockEditor {
        id: dayEditor
        // (Every day tints the matches; the day written in also shows the current one.)
        find: day.find && day.find.active ? day.find : null
        // :q / ZZ of Vim's keys: back in the history (the journal is often home: then nothing).
        onVimQuitRequested: (force) => AppController.back()
        objectName: "journalDayEditor"
        y: emptyPrompt.active ? emptyPrompt.y + emptyPrompt.height : headerRow.height
        width: day.width
        // As tall as the blocks (day.fitEditorHeight(), below): the journal list scrolls, not this
        // editor (the touch toolbar is docked on the stream's frame, not in the day).
        Component.onCompleted: day.fitEditorHeight()
        autoHeight: true
        scroller: day.scroller
        toolbarHost: day.toolbarHost
        stream: day.stream
        // A table cell being edited makes this the active day too (its toolbar shows).
        onEditingTableChanged: if (dayEditor.editingTable) day.activated()
        // The editor has its width from the start (a binding, not a layout's polish), so its
        // blocks are laid out at their final width right away.
        controller: page
        // The rows are drawn by the day's JournalDayItem until something needs them (materialize()).
        rowsDrawn: EditorInput.journalDayItem && !DeviceInfo.screenReader
        focus: true
        onReadOnlyLeaveRequested: AppSettings.readingMode = false
        onMessage: (text, kind, action) => {
            if (action) {
                day.messageAction = action
                day.messageToast = AppController.toastWithAction(text, kind, action.text)
            } else {
                AppController.toast(text, kind)
            }
        }
        listView.interactive: false
        maxTextWidth: AppSettings.editorWidth > 0 ? AppSettings.editorWidth : 100000
        // Typewriter scrolling and the editor's own keys as the shortcuts are now (ShortcutMap).
        typewriter: AppSettings.typewriterScrolling
        copyRefShortcut: AppController.routeText(ShortcutMap.sequences, "block.copyRef")
        sourceModeShortcut: AppController.routeText(ShortcutMap.sequences, "page.source")
        // The properties panel open or folded: a device setting, shared with pages. Bound here
        // too, not only by the Binding above (applied when the day is complete, after the editor
        // made its rows): a day created while the stream scrolls has its panel open at once —
        // a panel opened a moment later made the day grow and the content jump (docs/UI.md
        // "Scrolling performance", rule 1).
        propertiesExpanded: AppSettings.propertiesExpanded
        onPropertiesExpandedToggled: (expanded) => AppSettings.propertiesExpanded = expanded
        horizontalPadding: Theme.compact ? Theme.space3 : Theme.space8
        // The strip under the blocks (a tap adds a block at the end): a touch target on touch UIs,
        // reaching into the space between the days (the stream takes `footerOverhang` off it).
        bottomSpace: Theme.touchUi ? Theme.touchTarget : Theme.space3
        // Document days: headings stay below the date (the size of other days' dates).
        headingCap: day.compactHeader ? Theme.textXl : Theme.text2xl
        // A day switched to a document among outline days keeps their left edge (date, text);
        // when the journal is a document by default, its days use the full width.
        documentInset: Workspace.config.journalMode === "document" ? 0 : dayEditor.outlineGutter
    }

    // The editor is as tall as its blocks; an empty day shows the prompt instead (the editor
    // stays for its geometry, which the heading and the prompt use). Set, not bound: setting the
    // height can move the editor's list back into its bounds, which lays its rows out again and
    // changes their height within that same change (Ctrl+Z zooming back into a block, seen on
    // Windows) — a binding reported a loop there and kept the stale height; this sets it again
    // until it holds.
    property bool _fitting: false
    property bool _refit: false
    function fitEditorHeight(): void {
        if (day._fitting) {
            day._refit = true
            return
        }
        day._fitting = true
        for (let i = 0; i < 4; ++i) {
            day._refit = false
            dayEditor.height = day.empty && page.editingRow < 0 ? 0
                               : day.drawn ? day.drawnHeight + dayEditor.bottomSpace + dayEditor.listView.y
                               : dayEditor.listView.contentHeight + dayEditor.listView.y
            if (!day._refit)
                break
        }
        day._fitting = false
    }
    onEmptyChanged: day.fitEditorHeight()
    onDrawnHeightChanged: day.fitEditorHeight()
    Connections {
        target: page
        function onEditingRowChanged() { day.fitEditorHeight() }
    }

    // The embedded editor's list is exactly as tall as its content: what still scrolls it
    // (positioning a row) scrolls the journal list instead.
    Connections {
        target: dayEditor.listView
        function onContentHeightChanged() { day.fitEditorHeight() }
        function onYChanged() { day.fitEditorHeight() }
        function onContentYChanged() {
            const lv = dayEditor.listView
            const delta = lv.contentY - lv.originY
            if (Math.abs(delta) < 0.5)
                return
            lv.contentY = lv.originY
            if (day.scroller)
                day.scroller.scrollBy(delta)
        }
    }

    // ---- The blocks drawn by one item (BRULION_JOURNAL_DAY_ITEM=1) ---------------------------------
    /// The day's blocks are drawn by its JournalDayItem, not by its editor's rows (see the file
    /// comment): until materialize().
    readonly property bool drawn: dayEditor.rowsDrawn
    /// The drawn blocks' height (the editor's rows' sum, without the strip under them).
    readonly property real drawnHeight: day.drawnBlocks !== null ? day.drawnBlocks.contentHeight : 0
    final readonly property JournalDayItem drawnBlocks: drawnLoader.item as JournalDayItem
    /// A press on the drawn blocks lasts: their item stays (it handles the press) after the rows
    /// were made under it, drawing nothing; it goes when the press ends.
    property bool _drawnHeld: false
    /// The press forwards its moves to the editor's block selection (a mouse; a long press).
    property bool _dragForward: false
    /// The press was a long press (its release is no tap: as on the rows' markers).
    property bool _longPressed: false
    /// Points of a press (a call's point is read through a typed property), its keyboard modifiers.
    property point _scene
    property int _modifiers
    property point _mapped

    /// The rows were made (or the day was never drawn): materialize() does nothing more.
    property bool _materialized: !EditorInput.journalDayItem
    /// The editor's rows from now on (the same heights: nothing moves). The day keeps them.
    function materialize(): void {
        if (day._materialized)
            return
        day._materialized = true
        // (Set, not left to the binding: a screen reader turned on flips it too, and the rows must
        // be made here, in this order.)
        dayEditor.rowsDrawn = false
        // Every row now, in this event (the editor's list is as tall as its blocks).
        dayEditor.listView.forceLayout()
        day.fitEditorHeight()
    }
    /// The editor's delegate of `row` (null: none).
    function rowDelegate(row: int): BlockDelegate {
        return dayEditor.listView.itemAtIndex(row) as BlockDelegate
    }
    /// The BlockText of a delegate (a quote's sits in its frame).
    function blockTextOf(item: Item): BlockText {
        const text = item as BlockText
        if (text !== null)
            return text
        const n = item !== null ? EditorInput.childCount(item) : 0
        for (let i = 0; i < n; ++i) {
            const found = day.blockTextOf(EditorInput.childAt(item, i))
            if (found !== null)
                return found
        }
        return null
    }
    /// A tap (or a left click) on the drawn blocks at `scene`: what the editor's row does there.
    function forwardTap(part: string, row: int): void {
        // (The parameters copied first: compiled code reads none after a call. The press's scene
        // point and modifiers are in _scene and _modifiers.)
        const p = part
        const r = row
        const mods = day._modifiers
        const ed = dayEditor
        // A task box and a fold arrow change the page: the item follows, no rows are made for them.
        if (p === "taskBox") {
            page.toggleTask(r)
            return
        }
        if (p === "arrow") {
            page.toggleFold(r)
            return
        }
        if (p !== "footer" && p !== "number" && p !== "bullet" && p !== "text")
            return
        day.materialize()
        if (p === "footer") {
            // (Reading mode: a tap under the blocks, often the end of a scroll, does nothing.)
            if (ed.selectionMode || ed.readOnly)
                return
            const added = page.appendEmptyBlock()
            ed.startEditing(added, -1)
            return
        }
        const block = day.rowDelegate(r)
        if (block === null)
            return
        if (p === "number") {
            block.editOnTap(0)
        } else if (p === "bullet") {
            // A document has no zoom: the bullet is part of the text, a click edits the item.
            if (ed.blockTapped(r))
                return
            if (ed.documentMode)
                ed.startEditing(r, 0)
            else
                page.zoomInto(r)
        } else if (p === "text") {
            if (block.view === "rule") {
                block.editOnTap(-1)
                return
            }
            const text = day.blockTextOf(block.contentItem)
            if (text === null)
                return
            day._mapped = text.mapFromItem(null, day._scene.x, day._scene.y)
            const x = day._mapped.x
            const y = day._mapped.y
            // BlockText's tap: Shift+click, the touch selection mode; a link; else the caret here.
            if (ed.blockTappedWith(r, mods))
                return
            // (A copy: a string a method returned is not read again after another call.)
            const link = "" + text.linkAt(x, y)
            if (ed.openDateDialogAt(r, link))
                return
            if (link !== "" && !ed.editsOnClick(link))
                ed.activateLink(r, link)
            else
                ed.editAt(r, x, y)
        }
    }
    /// A right-click (mouse, touchpad, stylus) on the drawn blocks: the menu the row opens there.
    function forwardMenu(part: string, row: int): void {
        const p = part
        const r = row
        if (p === "footer" || p === "" || r < 0)
            return
        day.materialize()
        const ed = dayEditor
        const block = day.rowDelegate(r)
        if (block === null)
            return
        const drawnAt = day.drawnBlocks
        if (p === "bullet" || p === "number" || p === "taskBox") {
            // At the marker, as its own handler opens it (the delegate's coordinates).
            if (drawnAt === null)
                return
            day._mapped = drawnAt.menuPoint(r, p)
            if (p === "taskBox")
                ed.openTaskStatusMenu(r, block, day._mapped.x, day._mapped.y)
            else
                ed.openBlockMenu(r, block, day._mapped.x, day._mapped.y)
            return
        }
        if (p === "text" && block.view !== "rule") {
            const text = day.blockTextOf(block.contentItem)
            if (text !== null) {
                // (The rendered item sits at the BlockText's origin.)
                day._mapped = text.mapFromItem(null, day._scene.x, day._scene.y)
                const x = day._mapped.x
                const y = day._mapped.y
                const link = "" + text.linkAt(x, y)
                if (ed.openDateMenu(r, link, text.renderedItem, x, y)
                        || ed.openBlockLinkMenu(r, link, text.renderedItem, x, y))
                    return
                ed.openBlockMenuAt(r, day._scene)
                return
            }
        }
        // Elsewhere on a block: the list's handler, the menu at the pointer.
        ed.openBlockMenuAtScene(day._scene.x, day._scene.y)
    }
    /// A middle click on a link of the drawn text: that link in a new tab (BlockText).
    function forwardMiddle(part: string, row: int): void {
        const r = row
        if (part !== "text" || !OpenIn.tabs)
            return
        day.materialize()
        const block = day.rowDelegate(r)
        const text = block !== null && block.view !== "rule" ? day.blockTextOf(block.contentItem) : null
        if (text === null)
            return
        day._mapped = text.mapFromItem(null, day._scene.x, day._scene.y)
        dayEditor.openLinkInTab(r, text.linkAt(day._mapped.x, day._mapped.y))
    }
    /// A finger held on the drawn blocks (touch): the marker's menu, a token's or a link's menu, or
    /// the block selection (true: the finger's moves extend it, as on the editor's rows).
    function forwardLongPress(part: string, row: int): bool {
        const p = part
        const r = row
        if (p !== "bullet" && p !== "number" && p !== "taskBox" && p !== "text")
            return false
        day.materialize()
        const ed = dayEditor
        const block = day.rowDelegate(r)
        if (block === null)
            return false
        const drawnAt = day.drawnBlocks
        if (p === "bullet" || p === "number" || p === "taskBox") {
            if (drawnAt === null)
                return false
            day._mapped = drawnAt.menuPoint(r, p)
            if (p === "taskBox")
                ed.openTaskStatusMenu(r, block, day._mapped.x, day._mapped.y)
            else
                ed.openBlockMenu(r, block, day._mapped.x, day._mapped.y)
            return false
        }
        if (p !== "text" || block.view === "rule")
            return false
        const text = day.blockTextOf(block.contentItem)
        if (text === null)
            return false
        day._mapped = text.mapFromItem(null, day._scene.x, day._scene.y)
        const x = day._mapped.x
        const y = day._mapped.y
        const link = "" + text.linkAt(x, y)
        if (!ed.selectionMode && (ed.openDateMenu(r, link, text.renderedItem, x, y)
                                  || ed.openBlockLinkMenu(r, link, text.renderedItem, x, y)))
            return false
        ed.longPressText(r)
        return true
    }
    /// The "Add date" button of a drawn task: the editor's own button's menu (or "+": a reminder).
    function openDateFor(row: int): void {
        const r = row
        day.materialize()
        const block = day.rowDelegate(r)
        if (block === null)
            return
        if (block.addable) {
            dayEditor.addDateTo(r)
            return
        }
        // Its button shows while the menu is open: the menu opens under it.
        dayEditor.quickDateRow = r
        const button = day.dateButtonOf(block)
        dayEditor.openQuickDateMenu(r, button !== null ? button : block)
    }
    /// The "Add date" button of a delegate while it is shown (its Loader's item, "taskDateButton").
    function dateButtonOf(block: Item): Item {
        const n = EditorInput.childCount(block)
        for (let i = 0; i < n; ++i) {
            const loader = EditorInput.childAt(block, i) as Loader
            const item = loader !== null ? loader.item as Item : null
            if (item !== null && item.objectName === "taskDateButton")
                return item
        }
        return null
    }
    /// The link the pointer (or a pressing finger) is on, underlined: BlockText's rule (not a task
    /// box link, not a chip a click edits).
    function drawnLinkAt(x: real, y: real): string {
        if (day.drawnBlocks === null)
            return ""
        const link = "" + day.drawnBlocks.linkAt(x, y)
        if (link === "" || Js.startsWith(link, "task:") || dayEditor.editsOnClick(link))
            return ""
        return link
    }
    function drawnCursor(x: real, y: real): int {
        if (day.drawnBlocks === null)
            return Qt.ArrowCursor
        const part = "" + day.drawnBlocks.partAt(x, y)
        if (part === "text") {
            const link = "" + day.drawnBlocks.linkAt(x, y)
            if (link !== "" && (!dayEditor.editsOnClick(link) || dayEditor.opensDateDialog(link)))
                return Qt.PointingHandCursor
            return dayEditor.readOnly ? Qt.ArrowCursor : Qt.IBeamCursor
        }
        if (part === "bullet")
            return dayEditor.documentMode ? Qt.ArrowCursor : Qt.PointingHandCursor
        if (part === "arrow" || part === "dateButton")
            return Qt.PointingHandCursor
        if (part === "footer")
            return dayEditor.readOnly ? Qt.ArrowCursor : Qt.IBeamCursor
        return Qt.ArrowCursor
    }
    // The press ends: the item that took it goes (after its handlers are done).
    Later {
        id: drawnReleaseLater
        onTriggered: day._drawnHeld = false
    }
    // A screen reader: the editor's rows (each block an accessible item, links too).
    Connections {
        target: DeviceInfo
        function onScreenReaderChanged() {
            if (DeviceInfo.screenReader)
                day.materialize()
        }
    }

    Loader {
        id: drawnLoader
        active: day.drawn || day._drawnHeld
        x: dayEditor.x
        y: dayEditor.y + dayEditor.listView.y
        width: dayEditor.width
        sourceComponent: JournalDayItem {
            id: drawn
            objectName: "journalDayItem"
            // (An empty day shows its prompt instead: no strip under nothing.)
            height: day.empty ? 0 : drawn.contentHeight + drawn.footerHeight
            // Made rows under it during a press: it draws nothing more (the rows show) until it goes.
            opacity: day.drawn ? 1 : 0
            model: page.blocks
            blocked: dayEditor.titleFirst || page.conflict || page.zoomBlock >= 0 || page.editingRow >= 0
                     || page.selectionCount > 0 || page.sourceMode
            onDrawableChanged: if (!drawn.drawable) day.materialize()
            Component.onCompleted: if (!drawn.drawable) day.materialize()
            // The day's editor's geometry (BlockEditor, BlockDelegate), every level of it.
            documentMode: dayEditor.documentMode
            readOnly: dayEditor.readOnly
            touchUi: Theme.touchUi
            contentX: dayEditor.contentX
            contentWidth: dayEditor.contentWidth
            gutter: dayEditor.gutter
            indentStep: dayEditor.indentStep
            bulletColumn: dayEditor.bulletColumn
            arrowColumn: dayEditor.arrowColumn
            listIndent: dayEditor.listIndent
            taskBoxWidth: dayEditor.taskBoxWidth
            paragraphGap: dayEditor.paragraphGap
            listGap: dayEditor.listGap
            headingGapBelow: dayEditor.headingGapBelow
            headingGapsAbove: [0, dayEditor.headingGapAbove(1), dayEditor.headingGapAbove(2), dayEditor.headingGapAbove(3),
                               dayEditor.headingGapAbove(4), dayEditor.headingGapAbove(5), dayEditor.headingGapAbove(6)]
            fontSizes: [dayEditor.headingSize(0), dayEditor.headingSize(1), dayEditor.headingSize(2), dayEditor.headingSize(3),
                        dayEditor.headingSize(4), dayEditor.headingSize(5), dayEditor.headingSize(6)]
            lineHeights: [dayEditor.headingLineHeight(0), dayEditor.headingLineHeight(1), dayEditor.headingLineHeight(2),
                          dayEditor.headingLineHeight(3), dayEditor.headingLineHeight(4), dayEditor.headingLineHeight(5),
                          dayEditor.headingLineHeight(6)]
            firstLineCenters: [dayEditor.firstLineCenter(0), dayEditor.firstLineCenter(1), dayEditor.firstLineCenter(2),
                               dayEditor.firstLineCenter(3), dayEditor.firstLineCenter(4), dayEditor.firstLineCenter(5),
                               dayEditor.firstLineCenter(6)]
            spaces: [Theme.space1, Theme.space2, Theme.space3, Theme.space4, Theme.space5, Theme.space6]
            touchTarget: Theme.touchTarget
            iconSize: Theme.iconSizeSm
            boxRadius: Theme.radiusXs
            fontFamily: Theme.fontEditor
            semiBoldWeight: Theme.weightSemiBold
            normalWeight: Theme.weightNormal
            footerHeight: dayEditor.bottomSpace
            textColor: Theme.fg
            mutedColor: Theme.fgMuted
            faintColor: Theme.fgFaint
            linkColor: Theme.link
            guideColor: Theme.guide
            hoverColor: Theme.bgHover
            pressedColor: Theme.bgPressed
            successColor: Theme.success
            warningColor: Theme.warning
            warningTextColor: Theme.warningText
            controlBorderColor: Theme.borderControl
            checkColor: Theme.dark ? Theme.bg : "#ffffff"
            quoteBarColor: Theme.borderStrong
            ruleColor: Theme.border
            // Every day tints the stream's find bar's matches (BlockText's RenderedTextItem).
            findQuery: dayEditor.find !== null ? dayEditor.find.appliedQuery : ""
            findMatchCase: dayEditor.find !== null && dayEditor.find.matchCase
            findMatchDiacritics: dayEditor.find !== null && dayEditor.find.matchDiacritics
            findWholeWord: dayEditor.find !== null && dayEditor.find.wholeWord
            findColor: dayEditor.findHitColor
            Accessible.role: Accessible.List
            Accessible.name: DatesHelper.formatLong(day.date)

            // Hover: the row's fold arrow, the bullet's halo, a task box's tint, the link underlined,
            // the cursor and the link's preview (LinkPreviews) — drawn by the item.
            HoverHandler {
                id: drawnHover
                enabled: !Theme.touchUi
                cursorShape: day.drawnCursor(drawnHover.point.position.x, drawnHover.point.position.y)
                onPointChanged: {
                    const x = drawnHover.point.position.x
                    const y = drawnHover.point.position.y
                    drawn.setHover(x, y)
                    const link = drawnTap.pressed ? "" : day.drawnLinkAt(x, y)
                    drawn.hoveredLink = link
                    const preview = Js.startsWith(link, "day:") ? page.dateChipUrl(drawn.hoveredRow, link) : link
                    LinkPreviews.hover(drawn, preview, page.path, page.host, x, y)
                }
                onHoveredChanged: {
                    if (!drawnHover.hovered) {
                        LinkPreviews.leave(drawn)
                        drawn.clearHover()
                        drawn.hoveredLink = ""
                    }
                }
            }
            // A press: a mouse makes the rows at once (a drag from it selects blocks, as on the rows);
            // a finger waits for its tap or long press (most presses on a phone scroll the stream).
            // (On an item above the rest, as the editor's drag layer: a passive grab there sees the
            // presses whose point the tap handlers below take.)
            Item {
                anchors.fill: parent
                z: 2
                PointHandler {
                    id: drawnPointer
                    target: null
                    onActiveChanged: {
                        if (drawnPointer.active) {
                            drawnReleaseLater.stop()
                            day._drawnHeld = true
                            day._dragForward = false
                            day._longPressed = false
                            const device = drawnPointer.point.device
                            const mouse = device === null || device.type !== PointerDevice.TouchScreen
                            if (mouse) {
                                day.materialize()
                                dayEditor.dragPressed(drawnPointer.point.scenePosition, true)
                                day._dragForward = true
                            }
                            return
                        }
                        if (day._dragForward) {
                            day._dragForward = false
                            dayEditor.dragReleased()
                        }
                        drawnReleaseLater.restart()
                    }
                    onPointChanged: {
                        if (drawnPointer.active && day._dragForward)
                            dayEditor.dragMoved(drawnPointer.point.scenePosition)
                    }
                }
            }
            TapHandler {
                id: drawnTap
                gesturePolicy: TapHandler.ReleaseWithinBounds
                // The press feedback (phones have no hover): the link under the finger underlined.
                onPressedChanged: {
                    if (drawnTap.pressed && day.drawn) {
                        drawn.setHover(drawnTap.point.position.x, drawnTap.point.position.y)
                        drawn.hoveredLink = day.drawnLinkAt(drawnTap.point.position.x, drawnTap.point.position.y)
                    } else if (!drawnHover.hovered) {
                        drawn.hoveredLink = ""
                    }
                }
                onTapped: (eventPoint) => {
                    const x = eventPoint.position.x
                    const y = eventPoint.position.y
                    day._scene = eventPoint.scenePosition
                    day._modifiers = drawnTap.point.modifiers
                    if (day._longPressed)
                        return
                    const part = "" + drawn.partAt(x, y)
                    const row = drawn.rowAt(y)
                    day.forwardTap(part, row)
                }
            }
            // A finger held (BlockText's, the markers' long press): a menu or the block selection.
            TapHandler {
                id: drawnHold
                acceptedDevices: PointerDevice.TouchScreen
                enabled: Theme.touchUi
                onLongPressed: {
                    day._longPressed = true
                    day._scene = drawnHold.point.scenePosition
                    const x = drawnHold.point.position.x
                    const y = drawnHold.point.position.y
                    const part = "" + drawn.partAt(x, y)
                    const row = drawn.rowAt(y)
                    if (day.forwardLongPress(part, row))
                        day._dragForward = true
                }
            }
            TapHandler {
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
                acceptedButtons: Qt.RightButton
                gesturePolicy: TapHandler.WithinBounds
                onTapped: (eventPoint) => {
                    const x = eventPoint.position.x
                    const y = eventPoint.position.y
                    day._scene = eventPoint.scenePosition
                    const part = "" + drawn.partAt(x, y)
                    const row = drawn.rowAt(y)
                    day.forwardMenu(part, row)
                }
            }
            TapHandler {
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                acceptedButtons: Qt.MiddleButton
                enabled: OpenIn.tabs
                onTapped: (eventPoint) => {
                    const x = eventPoint.position.x
                    const y = eventPoint.position.y
                    day._scene = eventPoint.scenePosition
                    const part = "" + drawn.partAt(x, y)
                    const row = drawn.rowAt(y)
                    day.forwardMiddle(part, row)
                }
            }
            // Files dragged over the day: the editor's rows take them (its drop area).
            DropArea {
                anchors.fill: parent
                keys: ["text/uri-list"]
                onEntered: (drag) => {
                    drag.accepted = false
                    day.materialize()
                }
            }
            // An open task's "Add date" (with a date: "+") while its row is hovered, as on the rows.
            Loader {
                readonly property int row: drawn.hoveredRow
                readonly property rect area: row >= 0 && drawn.contentHeight >= 0 ? drawn.dateButtonArea(row)
                                                                                  : Qt.rect(0, 0, 0, 0)
                active: !Theme.touchUi && (day.drawn || day._drawnHeld) && area.width > 0
                x: area.x + (area.width - width) / 2
                y: area.y + (area.height - height) / 2
                sourceComponent: BIconButton {
                    objectName: "drawnDateButton"
                    label: drawn.addableAt(drawn.hoveredRow) ? qsTranslate("BlockDelegate", "Add a reminder or an alarm")
                                                             : qsTranslate("BlockDelegate", "Add date")
                    iconName: "calendar-plus"
                    size: "small"
                    focusPolicy: Qt.NoFocus
                    onClicked: day.openDateFor(drawn.hoveredRow)
                }
            }
        }
    }
}
