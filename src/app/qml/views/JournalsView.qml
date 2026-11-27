pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// JournalsView — the home view (docs/UI.md "Navigation"), Logseq style:
//   route "journals" ({date?, open?: "jump" | "write"}): today's journal on top, editable at once (the file is created by
//     the first edit, from the journal template when the vault has one — never just by looking),
//     then every earlier day that has a journal, newest first, each in its own embedded block
//     editor (ContentJournalDay; a lazy ListView over JournalListModel: only the days on screen
//     exist). Above them the missed reminders (ContentMissedBanner) and "Go to day" (a
//     calendar marking the days with journals). route.date scrolls to that day (or the nearest
//     earlier one).
//   route "journal" ({date, path?, source?}): one day as a page (PageView: header with the day
//     arrows, backlinks, side panel); a day without a note opens with the journal template and
//     is created by the first change (never just by looking at it).
// The touch toolbar of the day being edited docks on the stream's frame (`journals`), which the
// shell keeps on the keyboard: it never scrolls with the days, and the caret is kept above it.
// Blocks may be selected across several days (StreamSelection `stream`, docs/UI.md "Selecting
// and copying"): Shift+click, Shift+↑/↓ past a day's edge, a drag, a tap in the touch selection
// mode; such a selection is copied only.
// Ctrl+F (Ctrl+H: with replace) in the stream: the find bar (FindBar, docs/UI.md "Find and replace")
// over the stream's days (StreamFind): matches counted in every day, next / previous going on to
// the next day with one (scrolled to, made the day written in), every day on screen tinted, Replace
// all over all of them; the current match is in the day written in (NoteFind). One day as a page:
// its PageView has the bar. The notebook-wide search and replace cover all notes.
// Contract with the shell (qml/shell/ShellRouteView.qml): `route`, `inSidePanel`, `paneActive` (false:
// the other pane of a split view); offers `title` (one day: its date), `sidePanel`, `editing`,
// `toolbarDocked`, `endEditing()`, `canStepBack` / `stepBack()` (Back zooms out the zoom of the day
// written in first — a day zoomed into becomes that day), `sourceMode` and `viewState()` /
// `restoreViewState()` (the stream: the day at the top and how far into it; one day: its PageView's).
// With the editor drawn by Atrament (AppSettings.editorAtrament, `--atrament-editor`) the stream is
// one Atrament list instead of the ListView of ContentJournalDay items (docs/ATRAMENT.md "The journal
// stream as one list"; qml/editor/EditorAtramentJournal.qml, loaded by its file name): no QML item
// per day, one BlockEditor (`streamEditor`) acting on the day being written in.
FocusScope {
    id: root

    property var route: ({})
    property bool inSidePanel: false
    property bool paneActive: true

    /// Where the view is scrolled (a tab keeps it while another tab is shown): one day as a page
    /// — its PageView's; the stream — { date (the day at the top), offset (px into it) }.
    function viewState(): var {
        if (root.single)
            return pageTraits.viewState()
        if (root.streamView)
            return QmlCall.call(root.streamView, "viewState")
        return root.streamList ? root.streamViewState() : {}
    }
    function restoreViewState(state: var): void {
        if (root.single) {
            pageTraits.restoreViewState(state)
        } else if (root.streamList || root.streamView) {
            root.restoreStreamState(state)
        }
    }

    // ---- The journal stream's list and days (set by the stream while it exists) -------------------
    property ListView streamList: null
    property JournalListModel streamDays: null
    /// The stream drawn by Atrament (EditorAtramentJournal, an AtramentView reached by name) and its
    /// one editor; null with the QML stream.
    property Item streamView: null
    property BlockEditor streamEditor: null
    /// The stream is drawn by Atrament (decided when the stream is made).
    readonly property bool atramentStream: DeviceInfo.atrament && (AppSettings.editorAtrament || DevTools.atramentEditor)
    /// A view state to scroll back to once the days are known (pendingDate "": none).
    property string pendingDate
    property real pendingOffset
    /// The stream went to route.date (or to a restored view state) once.
    property bool streamScrolled: false
    /// The day (ContentJournalDay) of a stream row's item (null: none).
    function dayOf(item: Item): ContentJournalDay {
        if (!item)
            return null
        // The delegate's `day` (an inline delegate type: read as a property).
        return AppController.propertyOf(item, "day") as ContentJournalDay
    }
    /// The stream's item at (x, y) of its content (null: none).
    function streamItemAt(x: real, y: real): Item {
        return root.streamList.itemAt(x, y)
    }
    function streamItemAtIndex(row: int): Item {
        return root.streamList.itemAtIndex(row)
    }
    /// JournalsView.viewState() of the stream: { date (the day at the top), offset (px into it) }.
    function streamViewState(): var {
        const list = root.streamList
        const item = root.streamItemAt(list.width / 2, list.contentY + 1)
        const day = root.dayOf(item)
        if (day)
            return { date: day.date, offset: Math.round(list.contentY - item.y) }
        return {}
    }
    MapHolder { id: stateInfo }
    function restoreStreamState(state: var): void {
        // { date, offset }
        stateInfo.map = state
        root.pendingDate = AppController.routeText(stateInfo.map, "date")
        root.pendingOffset = AppController.routeInt(stateInfo.map, "offset", 0)
        if (root.pendingDate !== "")
            root.streamScrolled = true // not to route.date: where the tab was
        applyStateLater.restart()
    }
    function applyStreamState(): void {
        if (!root.streamList && !root.streamView)
            return
        const row = root.pendingDate !== "" ? root.rowOfDate(root.pendingDate) : -1
        if (row < 0)
            return // (once the days are known: onRefreshed)
        if (root.streamView) {
            // (The day, else the nearest earlier one: the stream's own rule.)
            QmlCall.call1(root.streamView, "restoreViewState", { date: root.pendingDate, offset: root.pendingOffset })
            root.pendingDate = ""
            return
        }
        root.pendingDate = ""
        root.streamList.positionViewAtIndex(row, ListView.Beginning)
        root.streamList.forceLayout()
        const item = root.streamItemAtIndex(row)
        if (item) {
            const top = root.streamList.originY
            const y = item.y + root.pendingOffset
            root.streamList.contentY = y > top ? y : top
        }
    }
    /// Make the day at `row` the one written in (the stream's find bar goes on there): scrolled
    /// to, created when it was not, activated.
    function activateDay(row: int): void {
        if (root.streamView) {
            QmlCall.call1(root.streamView, "activateDay", row)
            return
        }
        if (!root.streamList || row < 0 || row >= root.streamDays.count)
            return
        root.streamList.positionViewAtIndex(row, ListView.Contain)
        root.streamList.forceLayout()
        const day = root.dayOf(root.streamItemAtIndex(row))
        if (day)
            day.activated()
    }
    /// The stream's row of `date` or of the nearest earlier day with a note (-1: none).
    function rowOfDate(date: string): int {
        return root.streamDays.rowOfDate(date)
    }
    function scrollToDate(date: string): void {
        if (root.streamView) {
            QmlCall.call1(root.streamView, "scrollToDate", date)
            return
        }
        if (!root.streamList)
            return
        const row = root.rowOfDate(date)
        if (row >= 0)
            root.streamList.positionViewAtIndex(row, ListView.Beginning)
    }
    // Deferred calls (instead of Qt.callLater).
    Later {
        id: applyStateLater
        onTriggered: root.applyStreamState()
    }
    Later {
        id: scrollToRouteLater
        onTriggered: root.scrollToDate(AppController.routeText(root.route, "date"))
    }

    readonly property bool single: AppController.routeText(route, "view") === "journal"
    readonly property string date: AppController.routeText(route, "date") !== "" ? AppController.routeText(route, "date") : DatesHelper.today
    /// The day shown as a page (PageView; null in the stream).
    readonly property Item pageView: singleLoader.item as Item
    /// The parameters of a "journal" route its PageView gets.
    readonly property list<string> pageRouteKeys: ["line", "blockId", "heading", "source", "open"]
    /// The day whose editor had the caret last (side panel context, editing state).
    property ContentJournalDay activeDay: null
    // The stream's commands act on its editor's rows (a day drawn as one item makes them: docs/UI.md
    // "A journal day as one item").
    onActiveDayChanged: if (root.activeDay !== null) root.activeDay.materialize()
    /// The editor and the page of activeDay (null with none).
    readonly property BlockEditor activeEditor: streamEditor !== null ? (streamEditor.controller !== null ? streamEditor : null)
                                                : activeDay !== null ? activeDay.editor : null
    readonly property PageController activeController: activeEditor !== null ? activeEditor.controller : null
    /// Vim's mode of the day written in, the command typed or the command line (the status bar's
    /// left part; docs/UI.md "Vim mode"). One day shown: its page's.
    readonly property string statusVim: single ? pageTraits.statusVim
                                        : activeEditor !== null && activeEditor.vimSession.enabled
                                          && (activeEditor.activeFocus || activeEditor.vimSession.commandLine !== "")
                                          ? activeEditor.vimSession.status : ""

    readonly property string title: single ? DatesHelper.formatLong(date) : ""
    readonly property bool editing: single ? pageTraits.editing
                                           : (activeController !== null && activeController.editingRow >= 0)
    /// A touch toolbar is docked at the bottom (the shell keeps the content above it).
    readonly property bool toolbarDocked: single ? pageTraits.toolbarDocked
                                                 : (activeEditor !== null && activeEditor.toolbarVisible)
    /// The shell's quick capture from the bottom navigation: the editing of the day written in
    /// ends first.
    function endEditing(): void {
        if (single) {
            pageTraits.endEditing()
        } else if (activeEditor) {
            activeEditor.endTouchEditing()
        }
    }
    /// Back's own step before the history (the shell's Back key): the zoom of the day written in
    /// goes one level out (ContentJournalDay makes a day zoomed into the active one).
    readonly property bool canStepBack: single ? pageTraits.canStepBack
                                               : (activeEditor !== null && activeEditor.canStepBack)
    function stepBack(): void {
        if (single) {
            pageTraits.stepBack()
        } else if (activeEditor) {
            activeEditor.stepBack()
        }
    }
    /// One day as a page in source mode (the stream opens a day as a page for that).
    readonly property bool sourceMode: single && pageTraits.sourceMode
    /// The mode of the day being written in ("document" | "outline"; "" with none): the one the
    /// "toggleNoteMode" action switches.
    readonly property string noteMode: single ? pageTraits.noteMode
                                       : activeController !== null ? (activeController.documentMode ? "document" : "outline") : ""
    readonly property Component sidePanel: inSidePanel ? null : single ? pageTraits.sidePanel : journalsPanel

    focus: true

    PageTools { id: tools; workspace: Workspace }

    /// The stream's commands and shortcuts act on the day being written in (this pane, shown).
    readonly property bool streamKeys: !single && !inSidePanel && paneActive && visible && activeController !== null

    // The palette's "Toggle note mode", "Add page property", "Share as QR code…" and its link commands
    // ("copyLink:<kind>") in the journal stream: the day being written in (today until another day gets the caret). One day
    // as a page: its PageView does it.
    Connections {
        target: AppController
        function onUiActionRequested(action: string) {
            if (root.single || root.inSidePanel || !root.paneActive || !root.visible || !root.activeController)
                return
            if (Js.startsWith(action, "copyLink:")) {
                root.activeEditor.copyCurrentLink(Js.sliceFrom(action, 9))
                return
            }
            if (Js.startsWith(action, "extension:")) {
                root.activeEditor.runExtensionCommand(Js.sliceFrom(action, 10))
                return
            }
            if (action === "offerSelection") {
                Workspace.extensions.offerSelection(root.activeEditor.controller ? root.activeEditor.controller.path : "",
                                                    root.activeEditor.selectionForExtensions())
                return
            }
            if (Js.startsWith(action, "writing:")) {
                // The AI in the editor: the day being written in (docs/UI.md "AI in the editor").
                root.activeEditor.startWriting(Js.sliceFrom(action, 8), "")
                return
            }
            if (action === "saveWebPictures") {
                root.activeEditor.downloadWebPictures("")
                return
            }
            if (action === "findInNote" || action === "replaceInNote") {
                root.openFind(action === "replaceInNote")
                return
            }
            if (action === "toggleSourceMode") {
                // The day opens as a page in source mode (ContentJournalDay), as Ctrl+E there.
                root.activeEditor.toggleSourceMode()
                return
            }
            if (action === "addProperty") {
                root.activeEditor.addProperty()
                return
            }
            if (action === "taskClock") {
                root.activeEditor.toggleCurrentTaskClock()
                return
            }
            if (action === "taskDependency") {
                root.activeEditor.openCurrentDependencyPicker()
                return
            }
            if (action === "qrCode") {
                // The day being written in as QR codes, titled by its day (docs/UI.md "QR code").
                root.activeEditor.showPageQrCode("")
                return
            }
            if (action !== "toggleNoteMode" || root.activeEditor.readOnlyRefuses())
                return
            if (!root.activeController.toggleMode())
                AppController.toast(qsTr("The note mode could not be changed: the page's properties are not in a form Brulion can edit safely. Change them in the Markdown source (Ctrl+E)."), "error")
        }
    }

    // Ctrl+; in the stream: a property of the day being written in (one day as a page: its PageView).
    // (The shortcuts of these commands can be changed: ShortcutMap.)
    Shortcut {
        sequence: ShortcutMap.sequences["page.addProperty"] || ""
        enabled: !root.single && !root.inSidePanel && root.paneActive && root.visible && root.activeController !== null
                 && !root.activeController.sourceMode
        onActivated: root.activeEditor.addProperty()
    }
    // Markdown source of the day being written in (Ctrl+E): it opens as a page in source mode.
    Shortcut {
        sequence: ShortcutMap.sequences["page.source"] || ""
        enabled: !root.single && !root.inSidePanel && root.visible && root.activeController !== null
        onActivated: root.activeEditor.toggleSourceMode()
    }

    // ---- The find bar over the stream ------------------------------------------------------------
    property bool findOpen: false
    function openFind(withReplace: bool): void {
        if (root.single || !root.activeController)
            return
        if (!root.findOpen) {
            noteFind.setOriginHere(-1)
            root.findOpen = true
        }
        const bar = findBarLoader.item
        if (!bar)
            return
        if (withReplace)
            QmlCall.call(bar, "openReplace")
        else
            QmlCall.call(bar, "focusField")
    }
    function closeFind(): void {
        root.findOpen = false
        if (root.activeEditor)
            root.activeEditor.findClosed()
    }
    NoteFind {
        id: noteFind
        objectName: "journalFind"
        controller: root.activeController
        active: root.findOpen && !root.single
    }
    StreamFind {
        id: streamFind
        objectName: "journalStreamFind"
        host: Workspace
        model: root.streamDays
        find: noteFind
        // Another day has the next match: there it goes (the list scrolls, the day is written in).
        onDayRequested: (row, path) => root.activateDay(row)
    }
    // Loaded from its file when the bar first opens (docs/PERFORMANCE.md "QML files loaded when
    // first used"), with its properties.
    Loader {
        id: findBarLoader
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        active: root.findOpen && !root.single
        height: active && item !== null ? (item as Item).implicitHeight : 0
        z: 2
        onActiveChanged: {
            // (Closed: its file and values go too, so that it opens anew with the current ones.)
            if (!findBarLoader.active) {
                findBarLoader.source = ""
                return
            }
            if (findBarLoader.item !== null)
                return
            root._findBarValues = { find: noteFind, stream: streamFind, editor: root.activeEditor,
                                    placeholder: qsTr("Find in the journal") }
            findBarLoader.setSource(root._findBarSource, root._findBarValues)
        }
    }
    readonly property url _findBarSource: Qt.resolvedUrl("../editor/FindBar.qml")
    property var _findBarValues: ({})
    Binding { target: findBarLoader.item; property: "editor"; value: root.activeEditor; restoreMode: Binding.RestoreNone }
    Connections {
        target: findBarLoader.item
        function onCloseRequested(): void { root.closeFind() }
    }
    Shortcut {
        sequence: ShortcutMap.sequences["page.find"] || ""
        enabled: root.streamKeys
        onActivated: root.openFind(false)
    }
    Shortcut {
        sequence: ShortcutMap.sequences["page.replace"] || ""
        enabled: root.streamKeys
        onActivated: root.openFind(true)
    }

    // ---- One day ---------------------------------------------------------------------------------
    // (PageView loaded from its file when a day is first shown as a page, not named here: a QML
    // file named in another one is loaded with it — docs/PERFORMANCE.md "QML files loaded when
    // first used". Its optional members through ViewTraits, as the shell reads a view's.)
    Loader {
        id: singleLoader
        anchors.fill: parent
        active: root.single
        focus: root.single
        onActiveChanged: root.loadSingle()
        Component.onCompleted: root.loadSingle()
        onLoaded: pageTraits.applyPendingState()
    }
    /// The route of the day shown as a page.
    readonly property var singleRoute: AppController.routeWith({ view: "page", path: AppController.routeText(root.route, "path") !== ""
                                                                     ? AppController.routeText(root.route, "path") : tools.journalPathFor(root.date) },
                                                               root.route, root.pageRouteKeys)
    readonly property url _pageSource: Qt.resolvedUrl("PageView.qml")
    readonly property url _panelSource: Qt.resolvedUrl("../content/ContentSidePanel.qml")
    property var _panelValues: ({})
    property var _pageValues: ({})
    function loadSingle(): void {
        // (Back to the stream: the file and its first values go, a day shown again starts anew.)
        if (!singleLoader.active) {
            singleLoader.source = ""
            return
        }
        if (singleLoader.item !== null)
            return
        root._pageValues = { route: root.singleRoute, inSidePanel: root.inSidePanel, paneActive: root.paneActive, focus: true }
        singleLoader.setSource(root._pageSource, root._pageValues)
    }
    Binding { target: singleLoader.item; property: "route"; value: root.singleRoute; restoreMode: Binding.RestoreNone }
    Binding { target: singleLoader.item; property: "inSidePanel"; value: root.inSidePanel; restoreMode: Binding.RestoreNone }
    Binding { target: singleLoader.item; property: "paneActive"; value: root.paneActive; restoreMode: Binding.RestoreNone }
    ViewTraits {
        id: pageTraits
        view: singleLoader.item
    }

    // ---- The journal list ------------------------------------------------------------------------
    Loader {
        id: listLoader
        anchors.fill: parent
        anchors.topMargin: findBarLoader.height
        active: !root.single
        focus: !root.single
        sourceComponent: root.atramentStream ? atramentStreamComponent : journalsComponent
    }

    // Vim's mode in the corner where the shell's status bar does not show it (the stream's days;
    // one day shown has its page's).
    VimStatus {
        objectName: "journalVimStatus"
        session: root.activeEditor !== null ? root.activeEditor.vimSession : null
        visible: !root.single && root.statusVim !== "" && !(root.paneActive && AppController.statusBarShown)
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.space4
        // (Above a touch toolbar docked on the view.)
        anchors.bottomMargin: Theme.space2 + (root.toolbarDocked && root.activeEditor !== null
                                              ? root.activeEditor.toolbarHeight : 0)
        z: 2
    }

    Component {
        id: journalsComponent

        // The days' editors scroll with this list (BlockEditor.scroller, ContentJournalDay).
        EditorScroller {
            id: journals

            /// The toolbar of the day being edited is docked on this frame's bottom edge.
            readonly property bool toolbarDocked: root.activeDay !== null && root.activeEditor.toolbarVisible
            /// How far a drag selection may scroll the stream past the day it started in (that day
            /// must stay in the list's cache: destroyed, it would end the drag).
            selectionReach: list.dayCache / 2
            /// The day (ContentJournalDay) at a scene point, null in a gap or outside the days.
            function dayAt(scene: point): ContentJournalDay {
                journals.mapped = list.contentItem.mapFromItem(null, scene.x, scene.y)
                const y = Math.max(list.contentY, Math.min(list.contentY + list.height - 1, journals.mapped.y))
                return root.dayOf(root.streamItemAt(Math.max(0, Math.min(list.width - 1, journals.mapped.x)), y))
            }
            /// The editor of the day at a scene point (EditorScroller), null in a gap.
            function editorAt(scene: point): BlockEditor {
                const day = journals.dayAt(scene)
                // (A selection dragged into a day drawn as one item: its rows are made.)
                if (day)
                    day.materialize()
                return day ? day.editor : null
            }
            /// (A mapped point: a Q_INVOKABLE's point result is read through a typed property.)
            property point mapped
            /// Where the list scrolls (the autocomplete popup of a day follows it).
            readonly property real contentY: list.contentY

            /// Keep `rect` of `item` (a caret, a table cell) visible above the editing toolbar —
            /// the frame ends at the keyboard (the shell lifts it). Not while the keyboard slides:
            /// the day's editor asks again once it has settled.
            function ensureVisible(item: Item, rect: rect): void {
                // (The rect's fields first: a parameter is not read again after a call.)
                const rx = rect.x
                const ry = rect.y
                const rh = rect.height
                if (!item || KeyboardInsets.animating)
                    return
                journals.mapped = item.mapToItem(list.contentItem, rx, ry)
                const py = journals.mapped.y
                const margin = Theme.space8
                const bottom = list.height - (toolbarDocked ? Theme.editToolbarHeight : 0)
                const top = py - list.contentY
                if (top < margin)
                    list.contentY = Math.max(list.originY, py - margin)
                else if (top + rh > bottom - margin)
                    list.contentY = py + rh - bottom + margin
            }
            /// Typewriter scrolling (BlockEditor.typewriter): `rect` of `item` to the middle of
            /// the visible part, as far as the stream goes.
            function centerInView(item: Item, rect: rect): void {
                const rx = rect.x
                const ry = rect.y
                const rh = rect.height
                if (!item || KeyboardInsets.animating)
                    return
                journals.mapped = item.mapToItem(list.contentItem, rx, ry)
                const visibleHeight = list.height - (toolbarDocked ? Theme.editToolbarHeight : 0)
                const end = Math.max(list.originY, list.originY + list.contentHeight - list.height + list.bottomMargin)
                const target = Math.max(list.originY, Math.min(end, journals.mapped.y + rh / 2 - visibleHeight / 2))
                if (Math.abs(target - list.contentY) >= 1)
                    list.contentY = target
            }
            function keepCaretInView(): void {
                if (root.activeDay)
                    root.activeDay.keepCaretVisible()
            }
            function scrollBy(dy: real): void {
                const end = Math.max(list.originY, list.originY + list.contentHeight - list.height + list.bottomMargin)
                list.contentY = Math.max(list.originY, Math.min(end, list.contentY + dy))
            }
            /// A day's editor selects blocks with a finger (BlockEditor.setScrollingHeld): the stream
            /// does not flick meanwhile.
            function holdScrolling(held: bool): void {
                list.interactive = !held
            }
            /// The day model (StreamFind searches its days).
            readonly property JournalListModel daysModel: days
            /// Make the day at `row` the one written in (JournalsView.activateDay()).
            function activateDay(row: int): void {
                root.activateDay(row)
            }
            function scrollToDate(date: string): void {
                root.scrollToDate(date)
            }

            Component.onCompleted: {
                root.streamScrolled = false
                root.streamList = list
                root.streamDays = days
                // route.open "jump": the date picker right away (screenshots, smoke tests).
                if (AppController.routeText(root.route, "open") === "jump")
                    jumpLater.restart()
            }
            Component.onDestruction: {
                if (root.streamList === list) {
                    root.streamList = null
                    root.streamDays = null
                }
            }
            Later {
                id: jumpLater
                onTriggered: journals.openJumpDialog()
            }
            Later {
                id: caretLater
                onTriggered: journals.keepCaretInView()
            }

            // One selection over several days: the days' controllers attach while they exist.
            StreamSelection {
                id: stream
                objectName: "journalStreamSelection"
                host: Workspace
                model: days
                locale: DatesHelper.locale
                // The selection's end is in a day scrolled far away (Shift+↑/↓): scroll there.
                onRevealRequested: (path) => {
                    for (let i = 0; i < days.count; ++i) {
                        if (AppController.routeText(days.get(i), "path") === path) {
                            list.positionViewAtIndex(i, ListView.Contain)
                            return
                        }
                    }
                }
            }

            JournalListModel {
                id: days
                workspace: Workspace
                // Hidden (kept by ShellRouteView): edits only mark the days stale until it is shown.
                active: root.visible
                today: DatesHelper.today
                // route.date: once the days are known.
                onRefreshed: {
                    if (root.pendingDate !== "")
                        applyStateLater.restart()
                    if (!root.streamScrolled && AppController.routeFlag(root.route, "date") && count > 0) {
                        root.streamScrolled = true
                        scrollToRouteLater.restart()
                    }
                }
            }

            Connections {
                target: root
                function onRouteChanged() {
                    if (root.single || !AppController.routeFlag(root.route, "date"))
                        return
                    // Hidden (ShellRouteView sets the route before it shows the view): the days
                    // may be stale until it is shown — scroll once they are computed again.
                    if (days.active)
                        root.scrollToDate(AppController.routeText(root.route, "date"))
                    else
                        scrollToRouteLater.restart()
                }
            }

            Rectangle {
                anchors.fill: parent
                color: Theme.bg
            }

            ListView {
                id: list
                TouchFlick.enabled: true
                anchors.fill: parent
                focus: true
                clip: true
                model: days
                reuseItems: false
                // Days are prepared in the cache (between frames) about two screens ahead: a day is
                // created whole, with all its blocks, and is often taller than a screen
                // (ContentJournalDay).
                readonly property int dayCache: Math.max(1600, Math.round(2 * height))
                cacheBuffer: dayCache
                onDayCacheChanged: cacheBuffer = dayCache
                // Touchpad scrolling up: the cache above the view, as in BlockEditor's list — a day
                // created at the view's top edge that still grows (a query's results) pushed the
                // days being read; created above the view, it grows upwards.
                property real lastContentY: 0
                onContentYChanged: {
                    if (cacheBuffer > 0 && contentY < lastContentY - 0.5 && !dragging && !flicking)
                        cacheBuffer = cacheBuffer === dayCache ? dayCache + 1 : dayCache
                    lastContentY = contentY
                }
                // No current item: ListView gives its current item the focus whenever rows are
                // inserted or removed before it (a new day at midnight), which took the caret out
                // of the day being edited (tst_content_journals_qml).
                currentIndex: -1
                boundsBehavior: Flickable.StopAtBounds
                // Room for the docked toolbar.
                bottomMargin: Theme.space12 + (journals.toolbarDocked ? Theme.editToolbarHeight : 0)
                // The frame shrinks with the keyboard: the caret stays in view (growing, it covers
                // nothing: the list stays where the user scrolled it).
                property real lastHeight: 0
                onHeightChanged: {
                    const shrank = height < lastHeight - 0.5
                    lastHeight = height
                    if (shrank && journals.toolbarDocked)
                        caretLater.restart()
                }
                spacing: 0
                keyNavigationEnabled: false
                // No press delay: a Flickable that delays presses replays them on release without the
                // release for pointer handlers (Qt sends it only when an item grabbed the replayed press),
                // so a quick tap (< 90 ms) on block text, a link, a bullet or a fold arrow did nothing on
                // touch screens. Taps act on release anyway, and TextEdit moves the caret on release for
                // touch, so scrolling still never edits.
                pressDelay: 0
                // A mouse's left drag selects blocks in a day (BlockEditor), it does not scroll: the
                // wheel and the touchpad do; a finger flicks.
                acceptedButtons: Theme.touchUi ? Qt.LeftButton : Qt.NoButton
                BRowScrollBar { list: list }
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Journals")

                header: Item {
                    width: list.width
                    implicitHeight: headerColumn.implicitHeight + (Theme.compact ? Theme.space3 : Theme.space6)

                    ColumnLayout {
                        id: headerColumn
                        // The text column of the days' editors (BlockEditor.contentX / contentWidth,
                        // after the fold-arrow and bullet columns: BlockEditor.gutter).
                        readonly property real maxWidth: AppSettings.editorWidth > 0 ? AppSettings.editorWidth : 100000
                        readonly property real pad: Theme.compact ? Theme.space3 : Theme.space8
                        // (Days in the document mode have no gutter: the notebook's journal mode.)
                        readonly property real arrow: Workspace.config.journalMode === "document" ? 0
                                                      : Theme.touchUi ? 18 + 22 : 16 + 20
                        x: Math.max(pad, (list.width - maxWidth) / 2) + arrow
                        y: Theme.compact ? Theme.space3 : Theme.space5
                        width: Math.max(120, Math.min(maxWidth, list.width - 2 * pad)) - arrow
                        spacing: Theme.space3

                        // The missed reminders (ContentMissedBanner), loaded from its file only
                        // while there are some (docs/PERFORMANCE.md "QML files loaded when first used").
                        Loader {
                            Layout.fillWidth: true
                            active: Workspace.reminders !== null && Workspace.reminders.missedCount > 0
                            visible: active
                            source: Qt.resolvedUrl("../content/ContentMissedBanner.qml")
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.space2
                            BText {
                                Layout.fillWidth: true
                                text: Workspace.ready && days.count > 1
                                      ? qsTr("%n day(s) with notes", "", days.count - 1 + (days.todayExists ? 1 : 0))
                                      : Workspace.indexing ? qsTr("Reading the notebook…") : ""
                                variant: "caption"
                                color: Theme.fgFaint
                                elide: Text.ElideRight
                            }
                            BButton {
                                size: "small"
                                variant: "ghost"
                                iconName: "calendar-days"
                                text: qsTr("Go to day…")
                                onClicked: journals.openJumpDialog()
                            }
                        }
                    }
                }

                delegate: Item {
                    id: dayItem
                    required property int index
                    /// The day's editor (ContentJournalsSelfTest finds it through itemAtIndex()).
                    readonly property ContentJournalDay day: dayView
                    required property string date
                    required property string path
                    required property bool exists
                    required property bool isToday

                    width: list.width
                    // The day's tap strip under its blocks reaches past this item into the next
                    // day's gap (footerOverhang, on touch UIs): the spacing between days stays.
                    implicitHeight: dayView.implicitHeight - dayView.footerOverhang
                                    + (dayItem.index > 0 ? separator.height + 2 * gap : 0) + Theme.space2
                    height: implicitHeight
                    readonly property real gap: Theme.compact ? Theme.space5 : Theme.space8

                    Rectangle {
                        id: separator
                        visible: dayItem.index > 0
                        x: dayView.editor.contentX + dayView.editor.gutter
                        y: dayItem.gap
                        width: Math.max(0, dayView.editor.contentWidth - dayView.editor.gutter)
                        height: 1
                        color: Theme.borderSubtle
                    }
                    ContentJournalDay {
                        find: noteFind
                        streamFind: streamFind
                        id: dayView
                        y: dayItem.index > 0 ? separator.y + separator.height + dayItem.gap : 0
                        width: parent.width
                        date: dayItem.date
                        path: dayItem.path
                        exists: dayItem.exists
                        isToday: dayItem.isToday
                        scroller: journals
                        toolbarHost: journals
                        stream: stream
                        onActivated: {
                            // (A selection over several days keeps the other days' parts.)
                            if (root.activeDay && root.activeDay !== dayView && !stream.active) {
                                root.activeController.stopEditing()
                                root.activeController.selectedRow = -1
                            }
                            root.activeDay = dayView
                        }
                        Component.onCompleted: {
                            if (dayItem.isToday && root.activeDay === null)
                                root.activeDay = dayView
                            // route.open "write": start writing today (screenshots, smoke tests).
                            if (dayItem.isToday && AppController.routeText(root.route, "open") === "write")
                                beginLater.restart()
                        }
                        Component.onDestruction: if (root.activeDay === dayView) root.activeDay = null
                    }
                    Later {
                        id: beginLater
                        onTriggered: dayView.begin()
                    }
                }

                footer: Item {
                    width: list.width
                    height: days.count <= 1 && Workspace.ready ? emptyHint.implicitHeight + Theme.space12 : Theme.space8
                    BEmptyState {
                        id: emptyHint
                        objectName: "journalsEmptyHint"
                        visible: days.count <= 1 && Workspace.ready
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: Theme.space8
                        compact: true
                        iconName: "book-open"
                        title: qsTr("Your journal starts today")
                        // No-break spaces (and a word joiner after the hyphen, a break opportunity otherwise) keep
                        // "- [ ] …" and "⏰ jutro 9:00" on one line.
                        description: qsTr("Every day gets its own page. Write what happened, plans, tasks (-\u2060\u00a0[\u00a0]\u00a0…) and reminders (⏰\u00a0tomorrow\u00a09:00); earlier days appear below.")
                        // A new notebook (at most today's note): notes from another app are one tap away.
                        // (days.count first: only an empty journal follows the page count.)
                        actionText: days.count <= 1 && Workspace.pageCount <= 1 ? qsTr("Import notes") : ""
                        actionIcon: "download"
                        onActionTriggered: AppController.navigate({ view: "importExport", tab: "import" })
                    }
                }
            }

            // Dev: the journal stream's data-safety self-test (tests/app/content, ctest
            // tst_content_journals_qml).
            // Dev: selecting blocks across several days (ctest tst_content_journals_span_qml*).
            // Both loaded from their files (never named here: a file named is loaded with this one,
            // in every run), only in builds with the developer QML (BRULION_DEV_QML).
            Loader {
                id: selfTestLoader
                readonly property bool span: DevTools.editorScript === "journals-span-selftest"
                active: DevTools.editorScript === "journals-selftest" || span
                Component.onCompleted: {
                    if (!selfTestLoader.active)
                        return
                    journals.selfTestValues = { list: list, days: days, stream: stream }
                    journals.selfTestSource = selfTestLoader.span ? Qt.resolvedUrl("../content/ContentJournalsSpanSelfTest.qml")
                                                                  : Qt.resolvedUrl("../content/ContentJournalsSelfTest.qml")
                    selfTestLoader.setSource(journals.selfTestSource, journals.selfTestValues)
                }
            }
            // (setSource()'s arguments read from properties: compiled QML wants them so.)
            property url selfTestSource
            property var selfTestValues: ({})

            // "Go to day": loaded from its file the first time it opens (docs/PERFORMANCE.md "L10": its
            // month grid is not on the first screen; "QML files loaded when first used").
            Loader { id: jumpLoader }
            function openJumpDialog(): void {
                if (jumpLoader.item === null)
                    jumpLoader.source = Qt.resolvedUrl("JournalsJumpDialog.qml")
                const dialog = jumpLoader.item as T.Popup
                if (dialog)
                    dialog.open()
            }
            Connections {
                target: jumpLoader.item
                function onDayPicked(day: string): void {
                    if (days.exactRowOfDate(day) >= 0)
                        root.scrollToDate(day)
                    else
                        AppController.navigate({ view: "journal", date: day })
                }
            }
        }
    }

    // ---- The journal stream drawn by Atrament (one list, no item per day) ---------------------------
    Component {
        id: atramentStreamComponent

        // The stream's frame: the editor's touch toolbar docks on its bottom edge (it never scrolls).
        Item {
            id: frame

            /// The day being written in has its toolbar docked here.
            readonly property bool toolbarDocked: root.activeEditor !== null && root.activeEditor.toolbarVisible
            /// The editor's last message with an action (a toast's button runs it).
            property EditorToastAction messageAction: null
            property int messageToast: -1
            Connections {
                target: AppController
                function onToastActionTriggered(id: int) {
                    if (id === frame.messageToast && frame.messageAction)
                        frame.messageAction.run()
                }
            }

            Component.onCompleted: {
                root.streamScrolled = false
                root.streamDays = atramentDays
                root.streamEditor = streamEditor
                if (AppController.routeText(root.route, "open") === "jump")
                    atramentJumpLater.restart()
                streamLoader.load()
            }
            Component.onDestruction: {
                if (root.streamEditor === streamEditor) {
                    root.streamEditor = null
                    root.streamView = null
                    root.streamDays = null
                }
            }
            Later {
                id: atramentJumpLater
                onTriggered: frame.openJumpDialog()
            }
            Later {
                id: atramentBeginLater
                onTriggered: {
                    if (root.streamView)
                        QmlCall.call1(root.streamView, "beginDay", 0)
                }
            }

            // One selection over several days: the days' controllers attach while they are made.
            StreamSelection {
                id: atramentStream
                objectName: "journalStreamSelection"
                host: Workspace
                model: atramentDays
                locale: DatesHelper.locale
            }
            JournalListModel {
                id: atramentDays
                workspace: Workspace
                // Hidden (kept by ShellRouteView): edits only mark the days stale until it is shown.
                active: root.visible
                today: DatesHelper.today
                onRefreshed: {
                    if (root.pendingDate !== "")
                        applyStateLater.restart()
                    if (!root.streamScrolled && AppController.routeFlag(root.route, "date") && count > 0) {
                        root.streamScrolled = true
                        scrollToRouteLater.restart()
                    }
                    // route.open "write": start writing today (screenshots, smoke tests).
                    if (count > 0 && AppController.routeText(root.route, "open") === "write" && !frame.began) {
                        frame.began = true
                        atramentBeginLater.restart()
                    }
                }
            }
            property bool began: false
            Connections {
                target: root
                function onRouteChanged() {
                    if (root.single || !AppController.routeFlag(root.route, "date"))
                        return
                    if (atramentDays.active)
                        root.scrollToDate(AppController.routeText(root.route, "date"))
                    else
                        scrollToRouteLater.restart()
                }
            }

            Rectangle {
                anchors.fill: parent
                color: Theme.bg
            }
            // The stream's one editor: no rows of its own (the stream draws them), its geometry the
            // frame's, its controller the day written in (EditorAtramentJournal sets it).
            BlockEditor {
                id: streamEditor
                objectName: "journalDayEditor"
                width: frame.width
                height: 0
                atramentHost: root.streamView
                find: noteFind.active ? noteFind : null
                onVimQuitRequested: (force) => AppController.back()
                toolbarHost: frame
                stream: atramentStream
                onReadOnlyLeaveRequested: AppSettings.readingMode = false
                onMessage: (text, kind, action) => {
                    if (action) {
                        frame.messageAction = action
                        frame.messageToast = AppController.toastWithAction(text, kind, action.text)
                    } else {
                        AppController.toast(text, kind)
                    }
                }
                listView.interactive: false
                maxTextWidth: AppSettings.editorWidth > 0 ? AppSettings.editorWidth : 100000
                typewriter: AppSettings.typewriterScrolling
                copyRefShortcut: AppController.routeText(ShortcutMap.sequences, "block.copyRef")
                sourceModeShortcut: AppController.routeText(ShortcutMap.sequences, "page.source")
                propertiesExpanded: AppSettings.propertiesExpanded
                onPropertiesExpandedToggled: (expanded) => AppSettings.propertiesExpanded = expanded
                horizontalPadding: Theme.compact ? Theme.space3 : Theme.space8
                bottomSpace: Theme.touchUi ? Theme.touchTarget : Theme.space3
                headingCap: Theme.compact ? Theme.textXl : Theme.text2xl
                documentInset: Workspace.config.journalMode === "document" ? 0 : streamEditor.outlineGutter
            }
            // The stream itself (EditorAtramentJournal, an AtramentView: loaded by its file name).
            Loader {
                id: streamLoader
                anchors.fill: parent
                focus: true
                function load(): void {
                    frame.streamValues = { editorItem: streamEditor, journals: frame, daysModel: atramentDays,
                                           streamSelection: atramentStream, streamFind: streamFind }
                    streamLoader.setSource(frame.streamSource, frame.streamValues)
                }
                onLoaded: {
                    root.streamView = streamLoader.item as Item
                    if (root.pendingDate !== "")
                        applyStateLater.restart()
                }
            }
            readonly property url streamSource: Qt.resolvedUrl("../editor/EditorAtramentJournal.qml")
            property var streamValues: ({})

            // Dev: the stream's self-test against the list drawn by Atrament (ctest
            // tst_content_journals_atrament_qml), loaded from its file in developer builds only.
            Loader {
                id: atramentSelfTestLoader
                active: DevTools.editorScript === "journals-selftest" && root.streamView !== null
                onActiveChanged: {
                    if (!atramentSelfTestLoader.active)
                        return
                    frame.selfTestValues = { stream: root.streamView, editor: streamEditor, days: atramentDays,
                                             selection: atramentStream, streamFind: streamFind, journalsView: root }
                    atramentSelfTestLoader.setSource(frame.selfTestSource, frame.selfTestValues)
                }
            }
            property var selfTestValues: ({})
            readonly property url selfTestSource: Qt.resolvedUrl("../content/ContentJournalsAtramentSelfTest.qml")

            // "Go to day" (JournalsJumpDialog, loaded when it first opens).
            Loader { id: atramentJumpLoader }
            function openJumpDialog(): void {
                if (atramentJumpLoader.item === null)
                    atramentJumpLoader.source = Qt.resolvedUrl("JournalsJumpDialog.qml")
                const dialog = atramentJumpLoader.item as T.Popup
                if (dialog)
                    dialog.open()
            }
            Connections {
                target: atramentJumpLoader.item
                function onDayPicked(day: string): void {
                    if (atramentDays.exactRowOfDate(day) >= 0)
                        root.scrollToDate(day)
                    else
                        AppController.navigate({ view: "journal", date: day })
                }
            }
        }
    }

    Component {
        id: journalsPanel
        // (ContentSidePanel, loaded from its file when the panel is shown: docs/PERFORMANCE.md "QML
        // files loaded when first used".)
        Loader {
            id: journalsPanelLoader
            readonly property string path: root.activeController ? root.activeController.path : tools.journalPathFor(DatesHelper.today)
            Component.onCompleted: {
                root._panelValues = { path: journalsPanelLoader.path, controller: root.activeController, editor: root.activeEditor }
                journalsPanelLoader.setSource(root._panelSource, root._panelValues)
            }
            Binding { target: journalsPanelLoader.item; property: "path"; value: journalsPanelLoader.path; restoreMode: Binding.RestoreNone }
            Binding { target: journalsPanelLoader.item; property: "controller"; value: root.activeController; restoreMode: Binding.RestoreNone }
            Binding { target: journalsPanelLoader.item; property: "editor"; value: root.activeEditor; restoreMode: Binding.RestoreNone }
        }
    }
}
