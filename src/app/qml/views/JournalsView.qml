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
//     exist). Above them the missed reminders (ContentMissedBanner) and "Przejdź do dnia" (a
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
// Contract with the shell (qml/shell/ShellRouteView.qml): `route`, `inSidePanel`; offers `title`
// (one day: its date), `sidePanel`, `editing`, `toolbarDocked` and `sourceMode`.
FocusScope {
    id: root

    property var route: ({})
    property bool inSidePanel: false

    readonly property bool single: !!route && route.view === "journal"
    readonly property string date: route && route.date ? route.date : DatesHelper.today
    readonly property PageView pageView: singleLoader.item as PageView
    /// The day whose editor had the caret last (side panel context, editing state).
    property ContentJournalDay activeDay: null

    readonly property string title: single ? DatesHelper.formatLong(date) : ""
    readonly property bool editing: single ? (pageView !== null && pageView.editing)
                                           : (activeDay !== null && activeDay.editing)
    /// A touch toolbar is docked at the bottom (the shell hides its capture button).
    readonly property bool toolbarDocked: single ? (pageView !== null && pageView.toolbarDocked)
                                                 : (activeDay !== null && activeDay.editor.toolbarVisible)
    /// One day as a page in source mode (the stream opens a day as a page for that).
    readonly property bool sourceMode: single && pageView !== null && pageView.sourceMode
    readonly property Component sidePanel: inSidePanel ? null : single ? (pageView ? pageView.sidePanel : null) : journalsPanel

    focus: true

    PageTools { id: tools; workspace: Workspace }

    // The palette's "Przełącz tryb notatki", "Dodaj właściwość strony" and its link commands
    // ("copyLink:<kind>") in the journal stream: the day being written in (today until another day gets the caret). One day
    // as a page: its PageView does it.
    Connections {
        target: AppController
        function onUiActionRequested(action) {
            if (root.single || root.inSidePanel || !root.visible || !root.activeDay)
                return
            if (action.startsWith("copyLink:")) {
                root.activeDay.editor.copyCurrentLink(action.slice(9))
                return
            }
            if (action === "saveWebPictures") {
                root.activeDay.editor.downloadWebPictures("")
                return
            }
            if (action === "addProperty") {
                root.activeDay.editor.addProperty()
                return
            }
            if (action !== "toggleNoteMode")
                return
            if (!root.activeDay.controller.toggleMode())
                AppController.toast(qsTr("The note mode could not be changed: the page's properties are not in a form Brulion can edit safely. Change them in the Markdown source (Ctrl+E)."), "error")
        }
    }

    // Ctrl+; in the stream: a property of the day being written in (one day as a page: its PageView).
    Shortcut {
        sequence: "Ctrl+;"
        enabled: !root.single && !root.inSidePanel && root.visible && root.activeDay !== null
                 && !root.activeDay.controller.sourceMode
        onActivated: root.activeDay.editor.addProperty()
    }

    // ---- One day ---------------------------------------------------------------------------------
    Loader {
        id: singleLoader
        anchors.fill: parent
        active: root.single
        focus: root.single
        sourceComponent: PageView {
            inSidePanel: root.inSidePanel
            focus: true
            route: {
                const r = { view: "page", path: root.route.path ? root.route.path : tools.journalPathFor(root.date) }
                for (const key of ["line", "blockId", "heading", "source", "open"]) {
                    if (root.route[key] !== undefined)
                        r[key] = root.route[key]
                }
                return r
            }
        }
    }

    // ---- The journal list ------------------------------------------------------------------------
    Loader {
        id: listLoader
        anchors.fill: parent
        active: !root.single
        focus: !root.single
        sourceComponent: journalsComponent
    }

    Component {
        id: journalsComponent

        FocusScope {
            id: journals

            /// The toolbar of the day being edited is docked on this frame's bottom edge.
            readonly property bool toolbarDocked: root.activeDay !== null && root.activeDay.editor.toolbarVisible
            /// How far a drag selection may scroll the stream past the day it started in (that day
            /// must stay in the list's cache: destroyed, it would end the drag).
            readonly property real selectionReach: list.dayCache / 2
            /// The day (ContentJournalDay) at a scene point, null in a gap or outside the days.
            function dayAt(scene) {
                const p = list.contentItem.mapFromItem(null, scene.x, scene.y)
                const y = Math.max(list.contentY, Math.min(list.contentY + list.height - 1, p.y))
                const item = list.itemAt(Math.max(0, Math.min(list.width - 1, p.x)), y)
                for (let i = 0; item && i < item.children.length; ++i) {
                    const day = item.children[i] as ContentJournalDay
                    if (day)
                        return day
                }
                return null
            }
            /// Where the list scrolls (the autocomplete popup of a day follows it).
            readonly property real contentY: list.contentY

            /// Keep `rect` of `item` (a caret, a table cell) visible above the editing toolbar —
            /// the frame ends at the keyboard (the shell lifts it). Not while the keyboard slides:
            /// the day's editor asks again once it has settled.
            function ensureVisible(item, rect) {
                if (!item || KeyboardInsets.animating)
                    return
                const p = item.mapToItem(list.contentItem, rect.x, rect.y)
                const margin = Theme.space8
                const bottom = list.height - (toolbarDocked ? Theme.editToolbarHeight : 0)
                const top = p.y - list.contentY
                if (top < margin)
                    list.contentY = Math.max(list.originY, p.y - margin)
                else if (top + rect.height > bottom - margin)
                    list.contentY = p.y + rect.height - bottom + margin
            }
            function keepCaretInView() {
                if (root.activeDay)
                    root.activeDay.keepCaretVisible()
            }
            function scrollBy(dy) {
                const end = Math.max(list.originY, list.originY + list.contentHeight - list.height + list.bottomMargin)
                list.contentY = Math.max(list.originY, Math.min(end, list.contentY + dy))
            }
            /// A day's editor selects blocks with a finger (BlockEditor.setScrollingHeld): the stream
            /// does not flick meanwhile.
            function holdScrolling(held) {
                list.interactive = !held
            }
            function scrollToDate(date) {
                const row = days.rowOfDate(date)
                if (row >= 0)
                    list.positionViewAtIndex(row, ListView.Beginning)
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
                        if (days.get(i).path === path) {
                            list.positionViewAtIndex(i, ListView.Contain)
                            return
                        }
                    }
                }
            }

            JournalListModel {
                id: days
                workspace: Workspace
                today: DatesHelper.today
                property bool scrolled: false
                // route.date: once the days are known.
                onRefreshed: {
                    if (!scrolled && root.route && root.route.date && count > 0) {
                        scrolled = true
                        Qt.callLater(() => { if (journals && root && root.route) journals.scrollToDate(root.route.date) })
                    }
                }
            }
            // route.open "jump": the date picker right away (screenshots, smoke tests).
            Component.onCompleted: if (root.route && root.route.open === "jump") Qt.callLater(jumpDialog.open)

            Connections {
                target: root
                function onRouteChanged() {
                    if (!root.single && root.route && root.route.date)
                        journals.scrollToDate(root.route.date)
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
                // Room for the floating capture button on phones, and for the docked toolbar.
                bottomMargin: (Theme.narrow ? Theme.space16 + Theme.space8 : Theme.space12)
                              + (journals.toolbarDocked ? Theme.editToolbarHeight : 0)
                // The frame shrinks with the keyboard: the caret stays in view (growing, it covers
                // nothing: the list stays where the user scrolled it).
                property real lastHeight: 0
                onHeightChanged: {
                    const shrank = height < lastHeight - 0.5
                    lastHeight = height
                    if (shrank && journals.toolbarDocked)
                        Qt.callLater(journals.keepCaretInView)
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

                        ContentMissedBanner {
                            Layout.fillWidth: true
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
                                onClicked: jumpDialog.open()
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
                    implicitHeight: dayView.implicitHeight + (dayItem.index > 0 ? separator.height + 2 * gap : 0) + Theme.space2
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
                                root.activeDay.controller.stopEditing()
                                root.activeDay.controller.selectedRow = -1
                            }
                            root.activeDay = dayView
                        }
                        Component.onCompleted: {
                            if (dayItem.isToday && root.activeDay === null)
                                root.activeDay = dayView
                            // route.open "write": start writing today (screenshots, smoke tests).
                            if (dayItem.isToday && root.route && root.route.open === "write")
                                Qt.callLater(dayView.begin)
                        }
                        Component.onDestruction: if (root.activeDay === dayView) root.activeDay = null
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
                        actionText: Workspace.pageCount <= 1 ? qsTr("Import notes") : ""
                        actionIcon: "download"
                        onActionTriggered: AppController.navigate({ view: "importExport", tab: "import" })
                    }
                }
            }

            // Dev: the journal stream's data-safety self-test (tests/app/content, ctest
            // tst_content_journals_qml).
            Loader {
                active: DevTools.editorScript === "journals-selftest"
                sourceComponent: ContentJournalsSelfTest {
                    list: list
                    days: days
                    stream: stream
                }
            }
            // Dev: selecting blocks across several days (ctest tst_content_journals_span_qml*).
            Loader {
                active: DevTools.editorScript === "journals-span-selftest"
                sourceComponent: ContentJournalsSpanSelfTest {
                    list: list
                    days: days
                    stream: stream
                }
            }

            BDialog {
                id: jumpDialog
                parent: T.Overlay.overlay
                title: qsTr("Go to day")
                iconName: "calendar-days"
                showFooter: false
                preferredWidth: Theme.dialogWidthSm

                readonly property var marks: {
                    // (Read for real: a bare `void Workspace.revision` is compiled away with the dependency.)
                    const cells = jumpPicker.cells
                    if (!cells || cells.length === 0 || Workspace.revision < 0)
                        return ({})
                    const out = {}
                    for (const d of Workspace.journalDates(cells[0].date, cells[cells.length - 1].date))
                        out[d] = ["journal"]
                    return out
                }

                BDatePicker {
                    id: jumpPicker
                    Layout.fillWidth: true
                    selectedDate: DatesHelper.today
                    marks: jumpDialog.marks
                    onPicked: (value) => {
                        const d = value.slice(0, 10)
                        jumpDialog.close()
                        if (days.exactRowOfDate(d) >= 0)
                            journals.scrollToDate(d)
                        else
                            AppController.navigate({ view: "journal", date: d })
                    }
                }
                BText {
                    Layout.fillWidth: true
                    text: qsTr("Days with a dot have a journal. Type a date like “Tuesday” or “October 12”.")
                    variant: "muted"
                    wrapMode: Text.Wrap
                }
            }
        }
    }

    Component {
        id: journalsPanel
        ContentSidePanel {
            path: root.activeDay ? root.activeDay.path : tools.journalPathFor(DatesHelper.today)
            controller: root.activeDay ? root.activeDay.controller : null
            editor: root.activeDay ? root.activeDay.editor : null
        }
    }
}
