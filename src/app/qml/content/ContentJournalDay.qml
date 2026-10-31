pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// ContentJournalDay — one day of JournalsView: an overline ("DZIŚ · ŚRODA") over the date as a
// heading ("15 października 2026", a click opens the day as a page), "Otwórz jako stronę", and
// the day's note in an embedded block editor (its own PageController; the editor's list does not
// scroll — the journal list does — and is as tall as its blocks). A day without a note yet (today) shows a
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
FocusScope {
    id: day

    required property string date
    required property string path
    required property bool exists
    required property bool isToday
    /// The journal list: ensureVisible(item, rect) and scrollBy(dy) (any object with them).
    property var scroller: null
    /// Where the editor's touch toolbar docks (BlockEditor.toolbarHost); null: the day's editor.
    property Item toolbarHost: null
    /// The stream's selection over several days (JournalsView): the day's controller is attached
    /// to it while the day exists.
    property StreamSelection stream: null
    /// This day shows the tools of a selection over several days (it becomes the active day).
    readonly property bool spanTools: stream !== null && stream.active && stream.toolsController === page
    // (Also a day created again while scrolling: the keys, Back and the selection mode come here.)
    onSpanToolsChanged: {
        if (spanTools) {
            activated()
            editor.spanFocusHere(stream.focusRow, false)
        }
    }
    /// Compact windows: a smaller date and the open button as an icon (the overline shows at
    /// every width).
    property bool compactHeader: Theme.compact

    /// This day's editor got the caret (JournalsView stops editing in the others).
    signal activated()

    readonly property alias controller: page
    readonly property alias editor: editor
    readonly property bool editing: page.editingRow >= 0
    /// Nothing to show (also a day holding nothing but its note mode).
    readonly property bool empty: page.loaded && page.blocks.visibleCount === 0
    property bool _foldsRestored: false

    // The day is laid out with plain bindings, no Layouts: the journal list positions a day with
    // the height it has when it is created, and a layout's size only arrives with the next polish
    // — a day growing after the list had placed it pushed every day below it down while the list
    // scrolled up (docs/UI.md "Scrolling performance"). Its editor builds all its blocks at once
    // for the same reason (BlockEditor.autoHeight).
    implicitHeight: editor.y + editor.height
    /// How far the editor's tap strip under the blocks is taller than the space it shows: on touch
    /// UIs a touch target instead of 12 px. The journal stream lets it reach into the gap before
    /// the next day by that much, so the days keep their spacing (0 while the day shows no blocks).
    readonly property real footerOverhang: editor.height > 0 ? editor.bottomSpace - Theme.space3 : 0

    Accessible.role: Accessible.Grouping
    Accessible.name: DatesHelper.formatLong(day.date)

    /// Start writing in a day: a day without a note starts from the journal template (when the
    /// vault has one), the caret in its first empty block. Like a day opened on its own (PageView),
    /// the note is created by the first change, never by starting to write (startNewPage).
    function begin() {
        if (!page.loaded)
            return
        if (page.blocks.visibleCount > 0) {
            editor.startEditing(page.lastVisibleRow(), -1)
            return
        }
        editor.startEditing(page.startNewPage(page.exists ? "" : (Workspace.config.journalTemplate || "")), -1)
    }
    function openAsPage(source) {
        page.save()
        AppController.navigate(source ? { view: "journal", date: day.date, source: true }
                                       : { view: "journal", date: day.date })
    }
    function keepCaretVisible() {
        editor.ensureEditVisible()
    }
    function restoreFolds() {
        if (day._foldsRestored || !page.loaded)
            return
        const folds = Workspace.foldState(page.path)
        if (folds.length > 0)
            page.setFoldState(folds)
        day._foldsRestored = true
    }

    PageController {
        id: page
        objectName: "journalDayController"
        host: Workspace
        path: day.path
        allowNewBlockIds: !AppController.inDocsVault // the documentation's pages stay as shipped
        onLoadingChanged: if (page.loaded && !page.loading) day.restoreFolds()
        onFoldStateChanged: {
            if (day._foldsRestored && page.loaded && !page.loading)
                Workspace.setFoldState(page.path, page.foldState)
        }
        // A block selection in this day: the other days' selections end (one at a time) — not the
        // part of a selection over several days the stream gives it.
        onSelectionChanged: if (page.selectionCount > 0 && !(day.stream && day.stream.applying)) day.activated()
        onEditingRowChanged: {
            if (page.editingRow >= 0) {
                day.activated()
                // Through the editor's later(): the day may be gone by then (the stream left right
                // after a block started being edited — a palette command, a link).
                editor.later(() => editor.ensureEditVisible())
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
        restoreFolds()
        if (day.stream)
            day.stream.attach(page)
    }
    // The editor's delegates go first, while the editor still exists (they read its geometry).
    Component.onDestruction: {
        if (day.stream)
            day.stream.detach(page)
        editor.controller = null
    }

    // The end of a selection over several days moved into this day: it is the active one, its
    // editor has the keyboard (and on touch UIs the selection mode); the keys scroll to it.
    Connections {
        target: day.stream
        function onFocusMoved(controller, row, reveal) {
            if (controller !== page)
                return
            day.activated()
            day.editor.spanFocusHere(row, reveal)
        }
    }

    Connections {
        target: Workspace
        function onPathRenamed(oldPath, newPath) {
            if (page.path === oldPath)
                page.followRename(oldPath, newPath)
        }
        // The notebook's journal mode changed (settings, another device).
        function onConfigChanged() { page.refreshMode() }
    }

    // ---- The date -------------------------------------------------------------------------------
    // At every width the relative day and the weekday as a small uppercase line ("DZIŚ · ŚRODA",
    // "WCZORAJ · WTOREK", "PONIEDZIAŁEK"; today's in accentText) above "14 października 2026" —
    // no badge: the journal opened as a page and the calendar's day panel say "Dziś" as quietly.
    // Wide: today's date larger and "Otwórz jako stronę" at the right, centred on the heading;
    // compact windows: a smaller date and the open button as an icon.
    Item {
        id: headerRow
        x: editor.contentX + editor.gutter
        width: Math.max(0, day.width - x - Math.max(Theme.space2, editor.width - editor.contentX - editor.contentWidth))
        height: Math.max(dateColumn.height, openButton.visible ? openButton.implicitHeight : 0,
                         openIcon.visible ? openIcon.implicitHeight : 0)

        readonly property string longDate: DatesHelper.formatLong(day.date)
        readonly property int comma: longDate.indexOf(", ")
        readonly property string weekday: comma > 0 ? longDate.slice(0, comma) : ""
        readonly property string restOfDate: comma > 0 ? longDate.slice(comma + 2) : longDate
        readonly property string relative: day.isToday ? qsTr("Today")
                                            : day.date === DatesHelper.addDays(DatesHelper.today, -1) ? qsTr("Yesterday") : ""
        /// The button at the right end.
        readonly property real actionsWidth: (openButton.visible ? openButton.implicitWidth : 0)
                                             + (openIcon.visible ? openIcon.implicitWidth : 0)

        Item {
            id: dateColumn
            y: (headerRow.height - height) / 2
            width: Math.max(0, headerRow.width - headerRow.actionsWidth - (headerRow.actionsWidth > 0 ? Theme.space2 : 0))
            height: dateText.y + dateText.height
            BText {
                id: weekdayLine
                visible: text !== ""
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
                y: weekdayLine.visible ? weekdayLine.implicitHeight + (day.compactHeader ? 0 : Theme.space1) : 0
                width: parent.width
                text: headerRow.restOfDate
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
                x: editor.contentX + editor.gutter - Theme.space2
                width: Math.min(editor.contentWidth - editor.gutter + 2 * Theme.space2, parent.width - x - Theme.space2)
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
                    text: day.isToday ? qsTr("What's on today? Click to start writing.")
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
                HoverHandler { id: promptHover; enabled: !Theme.touchUi; cursorShape: Qt.IBeamCursor }
                TapHandler { id: promptTap; onTapped: day.begin() }
                BFocusRing { visible: prompt.activeFocus; targetRadius: Theme.radius }
            }
        }
    }

    // ---- The day's blocks -----------------------------------------------------------------------
    // The setting again whenever it changes (the panel's header writes it through
    // setPropertiesExpanded(), which replaces a plain binding).
    Binding {
        target: editor
        property: "propertiesExpanded"
        value: AppSettings.propertiesExpanded
    }
    BlockEditor {
        id: editor
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
        onEditingTableChanged: if (editingTable) day.activated()
        // The editor has its width from the start (a binding, not a layout's polish), so its
        // blocks are laid out at their final width right away.
        controller: page
        focus: true
        onMessage: (text, kind, action) => action ? AppController.toastAction(text, kind, action.text, action.run)
                                                  : AppController.toast(text, kind)
        listView.interactive: false
        maxTextWidth: AppSettings.editorWidth > 0 ? AppSettings.editorWidth : 100000
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
        documentInset: Workspace.config.journalMode === "document" ? 0 : outlineGutter
    }

    // The editor is as tall as its blocks; an empty day shows the prompt instead (the editor
    // stays for its geometry, which the heading and the prompt use). Set, not bound: setting the
    // height can move the editor's list back into its bounds, which lays its rows out again and
    // changes their height within that same change (Ctrl+Z zooming back into a block, seen on
    // Windows) — a binding reported a loop there and kept the stale height; this sets it again
    // until it holds.
    property bool _fitting: false
    property bool _refit: false
    function fitEditorHeight() {
        if (_fitting) {
            _refit = true
            return
        }
        _fitting = true
        for (let i = 0; i < 4; ++i) {
            _refit = false
            editor.height = empty && page.editingRow < 0 ? 0 : editor.listView.contentHeight + editor.listView.y
            if (!_refit)
                break
        }
        _fitting = false
    }
    onEmptyChanged: fitEditorHeight()
    Connections {
        target: page
        function onEditingRowChanged() { day.fitEditorHeight() }
    }

    // The embedded editor's list is exactly as tall as its content: what still scrolls it
    // (positioning a row) scrolls the journal list instead.
    Connections {
        target: editor.listView
        function onContentHeightChanged() { day.fitEditorHeight() }
        function onYChanged() { day.fitEditorHeight() }
        function onContentYChanged() {
            const lv = editor.listView
            const delta = lv.contentY - lv.originY
            if (Math.abs(delta) < 0.5)
                return
            lv.contentY = lv.originY
            if (day.scroller)
                day.scroller.scrollBy(delta)
        }
    }
}
