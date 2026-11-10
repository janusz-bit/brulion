pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// SearchView — route "search" ({query?, folder?}): full-text search over every note
// (SearchResultModel: folded words — "zolw" finds "Żółw" —, prefix of the last word, fuzzy, titles
// first) with the matched words highlighted in titles and excerpts, filters (All / Pages /
// Journals, a folder: the route's — the sidebar's "Search in folder", any depth — or a top-level
// one from the menu) and the recent searches when the field is empty. A result opens its page at
// the matching line. The query and the folder are kept in the route (back from a page returns to
// the results; a folder renamed or moved is followed).
//
// Keys: the field has the focus; Up/Down move through the results, Enter opens (Ctrl+Enter in
// the side panel), Escape clears.
// Contract with the shell (qml/shell/ShellRouteView.qml): `route`, optional `title`.
FocusScope {
    id: root

    property var route: ({})
    readonly property int maximumWidth: 860
    readonly property real sideMargin: Math.max(Theme.compact ? Theme.space3 : Theme.space6, (width - maximumWidth) / 2)
    property list<string> recent: []
    /// No highlighted matches (a daily note's title).
    readonly property list<var> noMatches: []

    readonly property string query: Js.trim(field.text)

    function loadRecent(): void {
        recent = AppSettings.recentSearches()
    }
    function remember(q: string): void {
        if (Js.trim(q) === "")
            return
        recent = AppSettings.rememberSearch(q)
    }
    function clearRecent(): void {
        recent = []
        AppSettings.clearRecentSearches()
    }
    MapHolder { id: resultInfo }
    /// Open result `row`: here, in the side panel (`newPane`) or in `target` ("tab": a new tab).
    function openResult(row: int, newPane: bool, target: string): void {
        // { path, line, … }
        resultInfo.map = results.get(row)
        const path = "" + resultInfo.map.path
        if (resultInfo.map.path === undefined || path === "")
            return
        remember(field.text)
        OpenIn.begin(target)
        AppController.openPage(path, resultInfo.map.line, newPane && !Theme.narrow)
        OpenIn.end()
    }
    /// Open the current result (the side panel with `newPane`).
    function openCurrent(newPane: bool): void {
        if (results.count > 0)
            openResult(Math.max(0, list.currentIndex), newPane, "")
    }
    function move(delta: int): void {
        if (results.count === 0)
            return
        list.currentIndex = Math.max(0, Math.min(results.count - 1, list.currentIndex + delta))
        list.positionViewAtIndex(list.currentIndex, ListView.Contain)
    }
    function applyQuery(q: string): void {
        field.text = q
        field.cursorPosition = q.length
        results.query = q
    }
    /// The route keeps the query and the folder (back returns to them; folder moves follow).
    function syncRoute(): void {
        if (AppController.routeText(AppController.currentRoute, "view") !== "search")
            return
        // (Empty parameters are left out of the route.)
        AppController.replaceRoute({ view: "search", query: query !== "" ? field.text : "",
                                     folder: results.folder })
    }
    function setFolder(folder: string): void {
        results.folder = folder
        syncRoute()
    }

    /// Take the route's query and folder (when they differ from the shown ones).
    function followRoute(): void {
        if (Js.hasKey(route, "query") && AppController.routeText(route, "query") !== field.text)
            applyQuery(AppController.routeText(route, "query"))
        const folder = AppController.routeText(route, "folder")
        if (folder !== results.folder)
            results.folder = folder
    }

    focus: true
    Component.onCompleted: {
        loadRecent()
        followRoute()
        if (visible)
            takeFocus()
    }
    /// The keyboard to the query (made shown, or shown the first time after being built ahead).
    function takeFocus(): void {
        field.forceActiveFocus()
    }
    onRouteChanged: followRoute()

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
            placeholderText: results.folder !== "" ? qsTr("Search in %1…").arg(results.folder) : qsTr("Search in all notes…")
            accessibleName: qsTr("Search")
            debounce: 180
            onSearch: (q) => {
                results.query = q
                // Back from a result comes back to these results.
                if (AppController.routeText(AppController.currentRoute, "query") !== q)
                    root.syncRoute()
            }
            onAccepted: root.openCurrent(false)
            Keys.onDownPressed: root.move(1)
            Keys.onUpPressed: root.move(-1)
            Keys.onPressed: (event) => {
                if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.ControlModifier)) {
                    root.openCurrent(true)
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
        // One row; on compact windows two — the scope alone across the width, then Replace…, the
        // folder and the count —, so the row never makes the whole view wider than a phone (the
        // scope with its icons and the buttons needed ~426 px in Polish).
        GridLayout {
            visible: root.query !== "" || results.folder !== ""
            Layout.fillWidth: true
            Layout.leftMargin: root.sideMargin
            Layout.rightMargin: root.sideMargin
            columns: Theme.compact ? 1 : 2
            columnSpacing: Theme.space2
            rowSpacing: Theme.space2
            BSegmented {
                Layout.fillWidth: Theme.compact
                fill: Theme.compact
                model: [{ value: "all", text: qsTr("All"), icon: "files" },
                        { value: "pages", text: qsTr("Pages"), icon: "file-text" },
                        { value: "journals", text: qsTr("Journals"), icon: "calendar-days" }]
                currentValue: results.scope
                size: "small"
                accessibleName: qsTr("Search in")
                onActivated: (value) => results.scope = value
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.space2
                BButton {
                    objectName: "searchReplace"
                    visible: !AppController.inDocsVault
                    size: "small"
                    variant: "ghost"
                    iconName: "pencil"
                    text: Theme.compact ? "" : qsTr("Replace…")
                    Accessible.name: qsTr("Replace in notebook")
                    onClicked: AppController.navigate({ view: "replace", query: root.query })
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
                    objectName: "searchCount"
                    text: results.count < results.totalCount ? qsTr("%1 of %n result(s)", "", results.totalCount).arg(results.count)
                                                             : qsTr("%n result(s)", "", results.count)
                    variant: "caption"
                    color: Theme.fgFaint
                }
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
            bottomMargin: Theme.space6
            highlightMoveDuration: 0
            T.ScrollBar.vertical: BScrollBar {}
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Search results")

            delegate: T.ItemDelegate {
                id: hit
                required property int index
                required property string title
                required property string folder
                required property string excerpt
                required property list<var> excerptMatches
                required property list<var> titleMatches
                required property string journalDate
                readonly property bool journal: hit.journalDate !== ""
                readonly property string heading: hit.journal ? DatesHelper.formatLong(hit.journalDate) : hit.title
                // On the highlighted row the accent is too light: the text colour (the matches
                // stay bold).
                readonly property color matchColor: hit.highlighted ? Theme.fg : Theme.accentText

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
                Accessible.name: hit.heading + ": " + hit.excerpt
                // Ctrl+click: a new tab (OpenIn).
                onClicked: root.openResult(hit.index, false, OpenIn.clickTarget(Qt.LeftButton, EditorInput.keyboardModifiers()))
                TapHandler {
                    // A middle click: in a new tab.
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    acceptedButtons: Qt.MiddleButton
                    enabled: OpenIn.tabs
                    onTapped: root.openResult(hit.index, false, "tab")
                }

                background: Rectangle {
                    visible: hit.down || hit.highlighted || hit.hovered
                    radius: Theme.radius
                    color: hit.down ? Theme.bgPressed : hit.highlighted ? Theme.bgSelected : Theme.bgHover
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
                            source: hit.heading
                            positions: hit.journal ? root.noMatches : hit.titleMatches
                            highlightColor: hit.matchColor
                            // The snippet's size: weight and colour set the title apart.
                            font.weight: Theme.weightSemiBold
                            font.pixelSize: Theme.textBase
                        }
                        BText {
                            visible: hit.folder !== "" && !hit.journal
                            Layout.maximumWidth: hitColumn.width * 0.4
                            text: hit.folder
                            variant: "caption"
                            // (fgFaint is AA on the page, not on the highlighted row.)
                            color: hit.highlighted ? Theme.fgMuted : Theme.fgFaint
                            elide: Text.ElideMiddle
                        }
                    }
                    ContentHighlightText {
                        visible: hit.excerpt !== ""
                        Layout.fillWidth: true
                        source: hit.excerpt
                        positions: hit.excerptMatches
                        highlightColor: hit.matchColor
                        color: Theme.fgMuted
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
                    root.setFolder("")
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
            onTriggered: root.setFolder("")
        }
        BMenuSeparator {}
    }
    Instantiator {
        model: results.folders
        // results.folders: [{ folder, count }]
        delegate: BMenuItem {
            required property string folder
            required property int count
            text: (folder !== "" ? folder : qsTr("Notebook root")) + "  (" + count + ")"
            iconName: "folder"
            checkable: true
            checked: results.folder === folder && folder !== ""
            enabled: folder !== ""
            onTriggered: root.setFolder(folder)
        }
        onObjectAdded: (index, object) => folderMenu.insertItem(index + 2, object as Item)
        onObjectRemoved: (index, object) => folderMenu.removeItem(object as Item)
    }
}
