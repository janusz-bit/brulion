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
//     arrows, backlinks, side panel); a day without a note opens empty and is created when typed.
// Contract with the shell (qml/shell/ShellRouteView.qml): `route`, `inSidePanel`; offers `title`
// (one day: its date), `sidePanel` and `editing`.
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
    readonly property Component sidePanel: inSidePanel ? null : single ? (pageView ? pageView.sidePanel : null) : journalsPanel

    focus: true

    PageTools { id: tools; workspace: Workspace }

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

            /// Keep `rect` of `item` (a caret) visible above the keyboard and the editing toolbar.
            function ensureVisible(item, rect) {
                if (!item)
                    return
                const p = item.mapToItem(list.contentItem, rect.x, rect.y)
                const margin = Theme.space8
                let bottom = list.height
                const kb = Qt.inputMethod.keyboardRectangle
                if (Qt.inputMethod.visible && kb.height > 0)
                    bottom = Math.min(bottom, list.mapFromItem(null, 0, kb.y / Screen.devicePixelRatio).y)
                if (Theme.touchUi)
                    bottom -= Theme.touchTarget + Theme.space2 + 1 // the editing toolbar
                const top = p.y - list.contentY
                if (top < margin)
                    list.contentY = Math.max(list.originY, p.y - margin)
                else if (top + rect.height > bottom - margin)
                    list.contentY = p.y + rect.height - bottom + margin
            }
            function scrollBy(dy) {
                list.contentY = Math.max(list.originY, list.contentY + dy)
            }
            function scrollToDate(date) {
                const row = days.rowOfDate(date)
                if (row >= 0)
                    list.positionViewAtIndex(row, ListView.Beginning)
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
                        Qt.callLater(() => journals.scrollToDate(root.route.date))
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
                anchors.fill: parent
                focus: true
                clip: true
                model: days
                reuseItems: false
                cacheBuffer: 900
                boundsBehavior: Flickable.StopAtBounds
                // Room for the floating capture button on phones.
                bottomMargin: Theme.narrow ? Theme.space16 + Theme.space8 : Theme.space12
                spacing: 0
                keyNavigationEnabled: false
                pressDelay: Theme.touchUi ? 90 : 0
                T.ScrollBar.vertical: BScrollBar {}
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
                        readonly property real arrow: Theme.touchUi ? 18 + 22 : 16 + 20
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
                        onActivated: {
                            if (root.activeDay && root.activeDay !== dayView) {
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
                        visible: days.count <= 1 && Workspace.ready
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: Theme.space8
                        compact: true
                        iconName: "book-open"
                        title: qsTr("Your journal starts today")
                        description: qsTr("Every day gets its own page. Write what happened, plans, tasks (- [ ] …) and reminders (⏰ jutro 9:00); earlier days appear below.")
                    }
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
                    void Workspace.revision
                    const cells = jumpPicker.cells
                    if (!cells || cells.length === 0)
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
                    text: qsTr("Days with a dot have a journal. Type a date like “wtorek” or “12 października”.")
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
