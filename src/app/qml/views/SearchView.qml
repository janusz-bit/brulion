pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// SearchView — route "search" ({query?}): full-text search over every note (SearchResultModel:
// folded words — "zolw" finds "Żółw" —, prefix of the last word, fuzzy, titles first) with the
// matched words highlighted in titles and excerpts, filters (Wszystko / Strony / Dzienniki, a
// folder) and the recent searches when the field is empty. A result opens its page at the
// matching line. The query is kept in the route (back from a page returns to the results).
//
// Keys: the field has the focus; Up/Down move through the results, Enter opens (Ctrl+Enter in
// the side panel), Escape clears.
// Contract with the shell (qml/shell/ShellRouteView.qml): `route`, optional `title`.
FocusScope {
    id: root

    property var route: ({})
    readonly property int maximumWidth: 860
    readonly property real sideMargin: Math.max(Theme.compact ? Theme.space3 : Theme.space6, (width - maximumWidth) / 2)
    property var recent: []

    readonly property string query: field.text.trim()

    function loadRecent() {
        const saved = AppSettings.value("search/recent", [])
        recent = Array.isArray(saved) ? saved.map(String) : (saved ? [String(saved)] : [])
    }
    function remember(q) {
        q = q.trim()
        if (q === "")
            return
        const key = TextHelper.fold(q)
        const list = [q].concat(recent.filter(r => TextHelper.fold(r) !== key)).slice(0, 8)
        recent = list
        AppSettings.setValue("search/recent", list)
    }
    function clearRecent() {
        recent = []
        AppSettings.setValue("search/recent", [])
    }
    function openResult(row, newPane) {
        const r = results.get(row)
        if (!r.path)
            return
        remember(field.text)
        AppController.openPage(r.path, r.line, newPane === true && !Theme.narrow)
    }
    function move(delta) {
        if (results.count === 0)
            return
        list.currentIndex = Math.max(0, Math.min(results.count - 1, list.currentIndex + delta))
        list.positionViewAtIndex(list.currentIndex, ListView.Contain)
    }
    function applyQuery(q) {
        field.text = q
        field.cursorPosition = q.length
        results.query = q
    }

    focus: true
    Component.onCompleted: {
        loadRecent()
        if (route && route.query)
            applyQuery(String(route.query))
        field.forceActiveFocus()
    }
    onRouteChanged: {
        if (route && route.query !== undefined && String(route.query) !== field.text)
            applyQuery(String(route.query))
    }

    SearchResultModel {
        id: results
        workspace: Workspace
        onQueryChanged: list.currentIndex = 0
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.bg
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: Theme.compact ? Theme.space3 : Theme.space6
        spacing: Theme.space3

        BSearchField {
            id: field
            Layout.fillWidth: true
            Layout.leftMargin: root.sideMargin
            Layout.rightMargin: root.sideMargin
            placeholderText: qsTr("Search in all notes…")
            accessibleName: qsTr("Search")
            debounce: 180
            onSearch: (q) => {
                results.query = q
                // Back from a result comes back to these results.
                if (AppController.currentRoute.view === "search" && String(AppController.currentRoute.query || "") !== q)
                    AppController.replaceRoute(q.trim() !== "" ? { view: "search", query: q } : { view: "search" })
            }
            onAccepted: if (results.count > 0) root.openResult(Math.max(0, list.currentIndex), false)
            Keys.onDownPressed: root.move(1)
            Keys.onUpPressed: root.move(-1)
            Keys.onPressed: (event) => {
                if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.ControlModifier)) {
                    if (results.count > 0)
                        root.openResult(Math.max(0, list.currentIndex), true)
                    event.accepted = true
                } else if (event.key === Qt.Key_PageDown) {
                    root.move(8)
                    event.accepted = true
                } else if (event.key === Qt.Key_PageUp) {
                    root.move(-8)
                    event.accepted = true
                }
            }
        }

        // ---- Filters ---------------------------------------------------------------------------------
        RowLayout {
            visible: root.query !== ""
            Layout.fillWidth: true
            Layout.leftMargin: root.sideMargin
            Layout.rightMargin: root.sideMargin
            spacing: Theme.space2
            BSegmented {
                model: [{ value: "all", text: qsTr("All"), icon: "files" },
                        { value: "pages", text: qsTr("Pages"), icon: "file-text" },
                        { value: "journals", text: qsTr("Journals"), icon: "calendar-days" }]
                currentValue: results.scope
                size: "small"
                accessibleName: qsTr("Search in")
                onActivated: (value) => results.scope = value
            }
            BButton {
                id: folderButton
                visible: results.folders.length > 1 || results.folder !== ""
                Layout.maximumWidth: Theme.compact ? 120 : 260
                size: "small"
                variant: results.folder !== "" ? "secondary" : "ghost"
                iconName: "folder"
                text: results.folder !== "" ? results.folder : Theme.compact ? "" : qsTr("Folder")
                Accessible.name: qsTr("Folder: %1").arg(results.folder !== "" ? results.folder : qsTr("all"))
                onClicked: folderMenu.popup(folderButton, 0, folderButton.height)
            }
            Item { Layout.fillWidth: true }
            BText {
                visible: !Theme.compact
                text: results.count < results.totalCount ? qsTr("%1 of %n result(s)", "", results.totalCount).arg(results.count)
                                                         : qsTr("%n result(s)", "", results.count)
                variant: "caption"
                color: Theme.fgFaint
            }
        }

        // ---- Results -----------------------------------------------------------------------------------
        ListView {
            id: list
            TouchFlick.enabled: true
            visible: root.query !== ""
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: results
            currentIndex: 0
            reuseItems: true
            boundsBehavior: Flickable.StopAtBounds
            bottomMargin: Theme.narrow ? Theme.space16 + Theme.space8 : Theme.space6
            highlightMoveDuration: 0
            T.ScrollBar.vertical: BScrollBar {}
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Search results")

            delegate: T.ItemDelegate {
                id: hit
                required property int index
                required property var model
                readonly property bool journal: hit.model.journalDate !== ""

                // Full width (ListView places delegates at x 0); the row itself is inset.
                width: list.width
                implicitHeight: hitColumn.implicitHeight + 2 * Theme.space2 + 2
                padding: Theme.space2
                leftInset: root.sideMargin - Theme.space2
                rightInset: root.sideMargin - Theme.space2
                leftPadding: root.sideMargin + Theme.space1
                rightPadding: root.sideMargin + Theme.space1
                hoverEnabled: !Theme.touchUi
                highlighted: ListView.isCurrentItem
                focusPolicy: Qt.NoFocus
                Accessible.role: Accessible.ListItem
                Accessible.name: (journal ? DatesHelper.formatLong(hit.model.journalDate) : hit.model.title) + ": " + hit.model.excerpt
                onClicked: root.openResult(hit.index, false)

                background: Rectangle {
                    radius: Theme.radius
                    color: hit.down ? Theme.bgPressed : hit.highlighted ? Theme.bgSelected : hit.hovered ? Theme.bgHover : "transparent"
                }
                contentItem: ColumnLayout {
                    id: hitColumn
                    spacing: 2
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space2
                        BIcon {
                            name: hit.journal ? "calendar-days" : "file-text"
                            size: Theme.iconSizeSm
                            color: Theme.fgMuted
                        }
                        ContentHighlightText {
                            Layout.fillWidth: true
                            source: hit.journal ? DatesHelper.formatLong(hit.model.journalDate) : hit.model.title
                            positions: hit.journal ? [] : hit.model.titleMatches
                            font.weight: Theme.weightSemiBold
                            font.pixelSize: Theme.textMd
                        }
                        BText {
                            visible: hit.model.folder !== "" && !hit.journal
                            Layout.maximumWidth: hitColumn.width * 0.4
                            text: hit.model.folder
                            variant: "caption"
                            color: Theme.fgFaint
                            elide: Text.ElideMiddle
                        }
                    }
                    ContentHighlightText {
                        visible: hit.model.excerpt !== ""
                        Layout.fillWidth: true
                        source: hit.model.excerpt
                        positions: hit.model.excerptMatches
                        color: Theme.fgMuted
                        font.family: Theme.fontEditor
                        font.pixelSize: Theme.textBase
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                    }
                }
            }

            BEmptyState {
                parent: list
                anchors.centerIn: parent
                width: Math.min(parent.width - 2 * Theme.space6, implicitWidth)
                visible: results.count === 0 && root.query !== "" && Workspace.ready
                iconName: "search"
                title: qsTr("Nothing found for “%1”").arg(root.query)
                description: results.totalCount > 0 ? qsTr("The filters hide %n result(s).", "", results.totalCount)
                                                     : qsTr("Try fewer or shorter words. Polish letters are optional: “zolw” finds “Żółw”.")
                actionText: results.totalCount > 0 ? qsTr("Show all results") : ""
                onActionTriggered: {
                    results.scope = "all"
                    results.folder = ""
                }
            }
        }

        // ---- Nothing typed yet: recent searches ----------------------------------------------------------
        Flickable {
            visible: root.query === ""
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentHeight: idle.implicitHeight + Theme.space8
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: idle
                x: root.sideMargin
                width: parent.width - 2 * root.sideMargin
                spacing: Theme.space2

                RowLayout {
                    visible: root.recent.length > 0
                    Layout.fillWidth: true
                    BText {
                        Layout.fillWidth: true
                        text: qsTr("Recent searches")
                        variant: "label"
                        Accessible.role: Accessible.Heading
                    }
                    BButton {
                        size: "small"
                        variant: "ghost"
                        text: qsTr("Clear")
                        onClicked: root.clearRecent()
                    }
                }
                Repeater {
                    model: root.recent
                    delegate: BListItem {
                        required property string modelData
                        Layout.fillWidth: true
                        dense: true
                        iconName: "history"
                        text: modelData
                        onClicked: {
                            root.applyQuery(modelData)
                            field.forceActiveFocus()
                        }
                    }
                }
                BEmptyState {
                    visible: root.recent.length === 0
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: Theme.space12
                    iconName: "search"
                    title: qsTr("Search your notes")
                    description: qsTr("Words anywhere in titles and text. Polish letters are optional: “zolw” finds “Żółw”; the last word may be the start of a word.")
                }
            }
        }
    }

    BMenu {
        id: folderMenu
        BMenuItem {
            text: qsTr("All folders")
            iconName: "files"
            checkable: true
            checked: results.folder === ""
            onTriggered: results.folder = ""
        }
        BMenuSeparator {}
    }
    Instantiator {
        model: results.folders
        delegate: BMenuItem {
            required property var modelData
            text: (modelData.folder !== "" ? modelData.folder : qsTr("Notebook root")) + "  (" + modelData.count + ")"
            iconName: "folder"
            checkable: true
            checked: results.folder === modelData.folder && modelData.folder !== ""
            enabled: modelData.folder !== ""
            onTriggered: results.folder = modelData.folder
        }
        onObjectAdded: (index, object) => folderMenu.insertItem(index + 2, object)
        onObjectRemoved: (index, object) => folderMenu.removeItem(object)
    }
}
