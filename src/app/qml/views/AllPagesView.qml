pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// AllPagesView — route "allPages" ({path?: folder, sort?}): every page of the notebook
// (PageListModel: built from the index, fast with thousands of pages) as a sortable list —
// title, folder, last change, backlinks, tags. Wide windows show columns (click a column to sort
// by it, again to reverse); phones show two-line rows and a sort menu. The filter finds titles,
// aliases and folders without Polish letters ("lodz" → "Łódź"); "Journals" adds the daily
// notes (the journals folder itself always lists them, without that chip). route.path lists one folder (the top bar's breadcrumb and the sidebar's "Show folder
// pages" lead here): its pages, with those of its subfolders unless "With subfolders" is off
// (remembered per device), and its subfolders as rows above them (at the root: the top-level
// folders) — a phone's folder browser; the top bar's breadcrumb goes back up. "New page"
// creates a page (in that folder). The rows' menus move and rename pages and folders through the
// shell (AppController.requestItemAction).
//
// Keys: typing goes to the filter; Down moves into the list; Enter opens; Ctrl+Enter opens in
// the side panel.
// Contract with the shell (qml/shell/ShellRouteView.qml): `route`, optional `title`.
FocusScope {
    id: root

    property var route: ({})
    readonly property string folder: AppController.routeText(route, "path")
    /// The folder's direct subfolders: [{ path, name, pages }] (Workspace.subfolders).
    readonly property list<var> subfolders: Workspace.revision >= 0 && Workspace.journalCount >= 0 && Js.trim(filterField.text) === ""
                                            ? Workspace.subfolders(root.folder) : []
    /// No highlighted matches (a daily note's title).
    readonly property list<var> noMatches: []
    readonly property bool wide: width >= 760
    readonly property int maximumWidth: 1100
    /// Side margin of the rows (and the toolbar and column headers above them).
    readonly property real rowMargin: wide ? Math.max(Theme.space6, (width - maximumWidth) / 2) : Theme.space2
    readonly property real toolbarMargin: wide ? rowMargin : Theme.space3
    /// The toolbar's width, from the view's: the filter field is sized from it, never from the
    /// Flow's own width — while the column cannot lay out (the view not sized yet: a tab's pane
    /// is 0 px high when it creates it) the Flow's width is its implicit width, which a field of
    /// the Flow's width made grow on every polish, without end.
    readonly property real toolbarWidth: Math.max(0, width - 2 * toolbarMargin)

    readonly property list<var> sortKeys: [
        { value: "title", text: qsTr("Title"), iconName: "file-text" },
        { value: "modified", text: qsTr("Changed"), iconName: "clock" },
        { value: "backlinks", text: qsTr("Backlinks"), iconName: "link" },
        { value: "folder", text: qsTr("Folder"), iconName: "folder" },
        { value: "tags", text: qsTr("Tags"), iconName: "tags" }
    ]
    /// The wide layout's column headers ({ key, text }).
    readonly property list<var> columnHeads: [
        { key: "title", text: qsTr("Title") }, { key: "folder", text: qsTr("Folder") },
        { key: "modified", text: qsTr("Changed") }, { key: "backlinks", text: qsTr("Links") },
        { key: "tags", text: qsTr("Tags") }
    ]

    function sortBy(key: string): void {
        if (pages.sortKey === key) {
            pages.descending = !pages.descending
        } else {
            pages.sortKey = key
            // Newest and most linked first; names A–Z.
            pages.descending = key === "modified" || key === "backlinks" || key === "tags"
        }
        AppSettings.setValue("allPages/sort", pages.sortKey + (pages.descending ? ":desc" : ":asc"))
    }
    /// Open row `row`'s page: here, in the side panel (`newPane`), or in `target` ("tab": a new tab).
    function open(row: int, newPane: bool, target: string): void {
        const path = "" + AppController.routeText(pages.get(row), "path")
        if (path !== "") {
            OpenIn.begin(target)
            AppController.openPage(path, -1, newPane && !Theme.narrow)
            OpenIn.end()
        }
    }
    function sortLabel(key: string): string {
        for (let i = 0; i < sortKeys.length; ++i) {
            if (AppController.routeText(sortKeys[i], "value") === key)
                return AppController.routeText(sortKeys[i], "text")
        }
        return key
    }
    function splitText(text: string, separator: string): list<string> {
        return Js.split(text, separator)
    }
    function flag(value: var): bool {
        return value === true || value === "true"
    }
    /// "#a #b" of a page's tags.
    function tagsText(tags: list<string>): string {
        return tags.length > 0 ? "#" + tags.join(" #") : ""
    }
    function nonEmpty(parts: list<string>): list<string> {
        return Js.nonEmptyStrings(parts)
    }

    focus: true
    Component.onCompleted: {
        const saved = root.splitText(AppController.routeFlag(route, "sort") ? AppController.routeText(route, "sort")
                                                                             : "" + AppSettings.value("allPages/sort", "title:asc"), ":")
        const key = saved.length > 0 ? saved[0] : ""
        const order = saved.length > 1 ? saved[1] : ""
        const parts = saved.length
        pages.sortKey = key !== "" ? key : "title"
        pages.descending = order === "desc" || (parts === 1 && (key === "modified" || key === "backlinks"))
        pages.includeJournals = root.flag(AppSettings.value("allPages/journals", false))
        pages.recursive = root.flag(AppSettings.value("allPages/subfolders", true))
        // Keyboard users can type to filter at once; on touch screens that would pop up the
        // on-screen keyboard over the list the user came to browse.
        if (!Theme.touchUi)
            filterField.forceActiveFocus()
    }

    PageListModel {
        id: pages
        workspace: Workspace
        folder: root.folder
        filter: filterField.text
        locale: DatesHelper.locale
        today: DatesHelper.today
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.bg
    }

    ColumnLayout {
        id: column
        anchors.fill: parent
        anchors.topMargin: Theme.compact ? Theme.space3 : Theme.space5
        spacing: Theme.space2

        // ---- Filter, sort, journals, new page --------------------------------------------------------
        Flow {
            id: toolbar
            Layout.fillWidth: true
            Layout.leftMargin: root.toolbarMargin
            Layout.rightMargin: root.toolbarMargin
            spacing: Theme.space2

            BSearchField {
                id: filterField
                width: root.wide ? Math.min(360, root.toolbarWidth * 0.45) : root.toolbarWidth
                placeholderText: root.folder !== "" ? qsTr("Filter pages in %1…").arg(root.folder) : qsTr("Filter pages…")
                debounce: 120
                Keys.onDownPressed: {
                    list.currentIndex = 0
                    list.forceActiveFocus()
                }
                onAccepted: if (pages.count > 0) root.open(0, false, "")
            }
            BButton {
                id: sortButton
                visible: !root.wide
                iconName: pages.descending ? "arrow-down" : "arrow-up"
                text: qsTr("Sort: %1").arg(root.sortLabel(pages.sortKey))
                onClicked: sortMenu.popup(sortButton, 0, sortButton.height)
            }
            BChip {
                // In the journals folder its daily notes are always listed.
                visible: !pages.journalsFolderListed
                text: qsTr("Journals")
                iconName: "calendar-days"
                interactive: true
                selected: pages.includeJournals
                height: Theme.controlHeight
                Accessible.name: qsTr("Show journals too")
                onClicked: {
                    pages.includeJournals = !pages.includeJournals
                    AppSettings.setValue("allPages/journals", pages.includeJournals)
                }
            }
            BChip {
                visible: root.folder !== ""
                text: root.folder
                iconName: "folder"
                removable: true
                height: Theme.controlHeight
                Accessible.name: qsTr("Only folder %1").arg(root.folder)
                onRemoveClicked: AppController.navigate({ view: "allPages" })
            }
            BChip {
                objectName: "allPagesSubfolders"
                text: qsTr("With subfolders")
                iconName: "folder-tree"
                interactive: true
                selected: pages.recursive
                height: Theme.controlHeight
                Accessible.name: qsTr("Pages of subfolders too")
                onClicked: {
                    pages.recursive = !pages.recursive
                    AppSettings.setValue("allPages/subfolders", pages.recursive)
                }
            }
            BButton {
                variant: "primary"
                iconName: "plus"
                text: qsTr("New page")
                onClicked: newPage.openIn(root.folder)
            }
        }

        BText {
            Layout.fillWidth: true
            Layout.leftMargin: root.toolbarMargin + 2
            Layout.rightMargin: root.toolbarMargin
            text: Js.trim(filterField.text) !== "" ? qsTr("%1 of %n page(s)", "", pages.totalCount).arg(pages.count)
                                                 : qsTr("%n page(s)", "", pages.totalCount)
            variant: "caption"
            color: Theme.fgFaint
        }

        // ---- Column headers (wide) -----------------------------------------------------------------------
        Item {
            visible: root.wide && pages.count > 0
            Layout.fillWidth: true
            Layout.leftMargin: root.rowMargin
            Layout.rightMargin: root.rowMargin
            implicitHeight: 30

            Row {
                anchors.fill: parent
                Repeater {
                    model: root.columnHeads
                    delegate: T.AbstractButton {
                        id: ch
                        required property string key
                        required text
                        readonly property bool active: pages.sortKey === key
                        readonly property bool alignRight: key === "backlinks"
                        width: key === "title" ? parent.width - columns.fixed : columns.widthOf(key)
                        height: parent.height
                        focusPolicy: Qt.TabFocus
                        hoverEnabled: !Theme.touchUi
                        Accessible.name: qsTr("Sort by %1").arg(text)
                        onClicked: root.sortBy(key)
                        contentItem: RowLayout {
                            spacing: 2
                            layoutDirection: ch.alignRight ? Qt.RightToLeft : Qt.LeftToRight
                            Item { visible: ch.alignRight; implicitWidth: Theme.space4 - 2 }
                            // The titles start after the rows' icon.
                            Item { visible: ch.key === "title"; implicitWidth: Theme.iconSizeSm + Theme.space2 * 2 - 2 }
                            BText {
                                text: ch.text
                                variant: "caption"
                                font.weight: ch.active ? Theme.weightSemiBold : Theme.weightMedium
                                color: ch.active ? Theme.fg : ch.hovered ? Theme.fgMuted : Theme.fgFaint
                            }
                            BIcon {
                                visible: ch.active
                                name: pages.descending ? "arrow-down" : "arrow-up"
                                size: Theme.iconSizeSm
                                color: Theme.fgMuted
                            }
                            Item { Layout.fillWidth: true }
                        }
                        BFocusRing { target: ch; visible: ch.visualFocus }
                    }
                }
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.borderSubtle
            }
        }

        // ---- The pages ------------------------------------------------------------------------------------
        ListView {
            id: list
            TouchFlick.enabled: true
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: pages
            currentIndex: -1
            keyNavigationEnabled: true
            boundsBehavior: Flickable.StopAtBounds
            reuseItems: true
            bottomMargin: Theme.narrow ? Theme.space16 + Theme.space8 : Theme.space6
            T.ScrollBar.vertical: BScrollBar {}
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Pages")

            // The folder's subfolders above its pages: a tap opens one (back returns).
            header: Column {
                width: list.width
                visible: root.subfolders.length > 0
                height: visible ? implicitHeight : 0
                bottomPadding: Theme.space2
                BText {
                    x: root.rowMargin + Theme.space2
                    text: qsTr("Folders")
                    variant: "caption"
                    color: Theme.fgFaint
                }
                Repeater {
                    model: root.subfolders
                    delegate: BListItem {
                        id: folderRow
                        required property string path
                        required property string name
                        required property int pages
                        objectName: "allPagesFolderRow"
                        x: root.rowMargin
                        width: list.width - 2 * root.rowMargin
                        iconName: path === AppController.routeText(Workspace.config, "journalsFolder") ? "calendar-days" : "folder"
                        text: name
                        count: folderRow.pages
                        Accessible.description: qsTr("%n page(s)", "", folderRow.pages)
                        onClicked: AppController.navigate({ view: "allPages", path: folderRow.path })
                        onPressAndHold: folderMenu.openFor(folderRow.path, folderRow)
                        TapHandler {
                            // Right-click only (a finger has no buttons: see the page rows).
                            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
                            acceptedButtons: Qt.RightButton
                            onTapped: folderMenu.openFor(folderRow.path, folderRow)
                        }
                    }
                }
            }

            Keys.onReturnPressed: (event) => root.open(currentIndex, (event.modifiers & Qt.ControlModifier) !== 0, "")
            Keys.onEnterPressed: (event) => root.open(currentIndex, (event.modifiers & Qt.ControlModifier) !== 0, "")
            Keys.onUpPressed: (event) => {
                if (currentIndex <= 0)
                    filterField.forceActiveFocus()
                else
                    event.accepted = false
            }

            // Column widths (wide layout).
            QtObject {
                id: columns
                readonly property real rowWidth: list.width - 2 * root.rowMargin
                readonly property int folder: Math.round(rowWidth * 0.18)
                readonly property int modified: 150
                readonly property int backlinks: 64
                readonly property int tags: Math.round(rowWidth * 0.22)
                readonly property int fixed: folder + modified + backlinks + tags
                function widthOf(key: string): int {
                    switch (key) {
                    case "folder": return folder
                    case "modified": return modified
                    case "backlinks": return backlinks
                    case "tags": return tags
                    }
                    return 0
                }
            }

            delegate: T.ItemDelegate {
                id: row
                required property int index
                required property string path
                required property string title
                required property string folder
                required property string journalDate
                required property string modifiedLabel
                required property int backlinks
                required property list<string> tags
                required property list<var> titleMatches

                readonly property string journal: row.journalDate
                readonly property string shownTitle: journal !== "" ? DatesHelper.formatLong(journal) : row.title
                /// The page's own icon (docs/UI.md "Page icon and cover"), following its edits.
                readonly property var pageIcon: Workspace.revision >= 0 ? Workspace.pageIconOf(row.path) : ({})

                // Full width (ListView places delegates at x 0); the row itself is inset.
                readonly property real margin: root.rowMargin
                width: list.width
                implicitHeight: root.wide ? Theme.listItemHeight : Theme.listItemHeightTwoLine
                leftInset: margin
                rightInset: margin
                leftPadding: margin + (root.wide ? 0 : Theme.space3)
                rightPadding: margin + (root.wide ? 0 : Theme.space3)
                hoverEnabled: !Theme.touchUi
                highlighted: ListView.isCurrentItem
                focusPolicy: Qt.NoFocus
                Accessible.role: Accessible.ListItem
                Accessible.name: shownTitle + (row.folder !== "" ? ", " + row.folder : "")
                // Ctrl+click: a new tab (OpenIn).
                onClicked: root.open(row.index, false, OpenIn.clickTarget(Qt.LeftButton, EditorInput.keyboardModifiers()))
                onPressAndHold: rowMenu.openFor(row.index, row)
                TapHandler {
                    // A middle click: in a new tab.
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    acceptedButtons: Qt.MiddleButton
                    enabled: OpenIn.tabs
                    onTapped: root.open(row.index, false, "tab")
                }
                TapHandler {
                    // Right-click only: Qt gives a finger (no buttons) to every TapHandler, so on
                    // a phone a tap opened the (modal) menu instead of the page.
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
                    acceptedButtons: Qt.RightButton
                    onTapped: rowMenu.openFor(row.index, row)
                }

                background: Rectangle {
                    radius: Theme.radius
                    color: row.down ? Theme.bgPressed : row.highlighted ? Theme.bgSelected : row.hovered ? Theme.bgHover : "transparent"
                    Rectangle {
                        visible: root.wide && !row.highlighted && !row.hovered
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 1
                        color: Theme.borderSubtle
                    }
                }

                contentItem: Item {
                    // Wide: one row of columns.
                    Row {
                        visible: root.wide
                        anchors.fill: parent
                        Item {
                            width: parent.width - columns.fixed
                            height: parent.height
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: Theme.space2
                                anchors.rightMargin: Theme.space3
                                spacing: Theme.space2
                                BPageIcon {
                                    icon: row.pageIcon
                                    fallback: row.journal !== "" ? "calendar-days" : "file-text"
                                    size: Theme.iconSizeSm
                                    color: Theme.fgFaint
                                }
                                ContentHighlightText {
                                    Layout.fillWidth: true
                                    source: row.shownTitle
                                    positions: row.journal !== "" ? root.noMatches : row.titleMatches
                                    font.weight: Theme.weightMedium
                                }
                            }
                        }
                        BText {
                            width: columns.folder
                            anchors.verticalCenter: parent.verticalCenter
                            rightPadding: Theme.space3
                            text: row.folder
                            variant: "muted"
                            elide: Text.ElideMiddle
                        }
                        BText {
                            width: columns.modified
                            anchors.verticalCenter: parent.verticalCenter
                            rightPadding: Theme.space3
                            text: row.modifiedLabel
                            variant: "muted"
                            elide: Text.ElideRight
                        }
                        BText {
                            width: columns.backlinks
                            anchors.verticalCenter: parent.verticalCenter
                            rightPadding: Theme.space4
                            horizontalAlignment: Text.AlignRight
                            text: row.backlinks > 0 ? "" + row.backlinks : "–"
                            variant: row.backlinks > 0 ? "body" : "faint"
                        }
                        BText {
                            width: columns.tags
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.tagsText(row.tags)
                            variant: "small"
                            color: Theme.tagFg
                            elide: Text.ElideRight
                        }
                    }

                    // Narrow: title + one line of details.
                    ColumnLayout {
                        visible: !root.wide
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width
                        spacing: 2
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.space2
                            BPageIcon {
                                visible: width > 0
                                icon: row.pageIcon
                                size: Theme.iconSizeSm
                                color: Theme.fgFaint
                            }
                            ContentHighlightText {
                                Layout.fillWidth: true
                                source: row.shownTitle
                                positions: row.journal !== "" ? root.noMatches : row.titleMatches
                                font.weight: Theme.weightMedium
                                font.pixelSize: Theme.textMd
                            }
                        }
                        BText {
                            Layout.fillWidth: true
                            text: root.nonEmpty([row.folder,
                                                 row.modifiedLabel,
                                                 row.backlinks > 0 ? qsTr("%n link(s)", "", row.backlinks) : "",
                                                 root.tagsText(row.tags)]).join(" · ")
                            variant: "small"
                            color: Theme.fgMuted
                            elide: Text.ElideRight
                        }
                    }
                }
            }

            BEmptyState {
                parent: list
                anchors.centerIn: parent
                width: Math.min(parent.width - 2 * Theme.space6, implicitWidth)
                visible: pages.count === 0 && Workspace.ready && root.subfolders.length === 0
                iconName: Js.trim(filterField.text) !== "" ? "search" : "files"
                title: Js.trim(filterField.text) !== "" ? qsTr("No page matches “%1”").arg(Js.trim(filterField.text))
                                                      : root.folder !== "" ? qsTr("This folder has no pages") : qsTr("No pages yet")
                description: Js.trim(filterField.text) !== ""
                             ? qsTr("The filter looks at titles, aliases and folders. Full-text search finds words inside notes.")
                             : qsTr("Pages are Markdown files in the notebook folder. Create one, or link to a new page with [[Name]] anywhere.")
                actionText: Js.trim(filterField.text) !== "" ? qsTr("Search inside notes") : qsTr("New page")
                actionIcon: Js.trim(filterField.text) !== "" ? "search" : "plus"
                onActionTriggered: {
                    if (Js.trim(filterField.text) !== "")
                        AppController.search(Js.trim(filterField.text))
                    else
                        newPage.openIn(root.folder)
                }
            }
            BSpinner {
                parent: list
                anchors.centerIn: parent
                visible: pages.count === 0 && !Workspace.ready
                size: 28
            }
        }
    }

    Instantiator {
        model: root.sortKeys
        // sortKeys: { value, text, iconName }
        delegate: BMenuItem {
            id: sortItem
            required property string value
            required text
            required iconName
            checkable: true
            checked: pages.sortKey === value
            onTriggered: root.sortBy(sortItem.value)
        }
        onObjectAdded: (index, object) => sortMenu.insertItem(index, object as Item)
        onObjectRemoved: (index, object) => sortMenu.removeItem(object as Item)
    }
    BMenu {
        id: sortMenu
        BMenuSeparator {}
        BMenuItem {
            text: pages.descending ? qsTr("Ascending") : qsTr("Descending")
            iconName: pages.descending ? "arrow-up" : "arrow-down"
            onTriggered: {
                pages.descending = !pages.descending
                AppSettings.setValue("allPages/sort", pages.sortKey + (pages.descending ? ":desc" : ":asc"))
            }
        }
    }

    BMenu {
        id: rowMenu
        property int row: -1
        // The row's page ({ path, journalDate, … }).
        readonly property string pagePath: row >= 0 ? AppController.routeText(pages.get(row), "path") : ""
        readonly property string pageJournalDate: row >= 0 ? AppController.routeText(pages.get(row), "journalDate") : ""
        function openFor(r: int, item: Item): void {
            row = r
            popup(item, Math.min(item.width - width, item.width / 2), item.height / 2)
        }
        BMenuItem {
            text: qsTr("Open")
            iconName: "file-text"
            onTriggered: root.open(rowMenu.row, false, "")
        }
        BMenuItem {
            visible: OpenIn.tabs
            height: visible ? implicitHeight : 0
            text: qsTr("Open in new tab")
            iconName: "plus"
            onTriggered: root.open(rowMenu.row, false, "tab")
        }
        BMenuItem {
            visible: !Theme.narrow
            height: visible ? implicitHeight : 0
            text: qsTr("Open in side panel")
            iconName: "panel-right"
            onTriggered: root.open(rowMenu.row, true, "")
        }
        BMenuItem {
            objectName: "allPagesMenuMove"
            text: qsTr("Move to…")
            iconName: "folder-input"
            onTriggered: AppController.requestItemAction("move", rowMenu.pagePath)
        }
        BMenuItem {
            visible: rowMenu.pageJournalDate === ""
            height: visible ? implicitHeight : 0
            text: qsTr("Rename…")
            iconName: "pencil"
            onTriggered: AppController.requestItemAction("rename", rowMenu.pagePath)
        }
        BMenuItem {
            text: qsTr("Version history")
            iconName: "history"
            onTriggered: AppController.navigate({ view: "history", path: rowMenu.pagePath })
        }
        BMenuSeparator {}
        BMenuItem {
            text: qsTr("Move to trash")
            iconName: "trash-2"
            danger: true
            onTriggered: deleteDialog.openFor(rowMenu.pagePath)
        }
    }
    // A subfolder row's menu: the folder's actions live in the shell (its dialogs).
    BMenu {
        id: folderMenu
        objectName: "allPagesFolderMenu"
        property string path
        function openFor(p: string, item: Item): void {
            path = p
            popup(item, Math.min(item.width - width, item.width / 2), item.height / 2)
        }
        BMenuItem {
            text: qsTr("Open")
            iconName: "folder-open"
            onTriggered: AppController.navigate({ view: "allPages", path: folderMenu.path })
        }
        BMenuItem {
            text: qsTr("Move to…")
            iconName: "folder-input"
            onTriggered: AppController.requestItemAction("move", folderMenu.path)
        }
        BMenuItem {
            text: qsTr("Rename…")
            iconName: "pencil"
            onTriggered: AppController.requestItemAction("rename", folderMenu.path)
        }
        BMenuItem {
            text: qsTr("Show in tree")
            iconName: "folder-tree"
            onTriggered: AppController.requestItemAction("reveal", folderMenu.path)
        }
        BMenuSeparator {}
        BMenuItem {
            text: qsTr("Move to trash")
            iconName: "trash-2"
            danger: true
            onTriggered: AppController.requestItemAction("delete", folderMenu.path)
        }
    }

    ContentNewPageDialog {
        id: newPage
        parent: T.Overlay.overlay
    }
    BConfirmDialog {
        id: deleteDialog
        property string path
        function openFor(p: string): void {
            path = p
            open()
        }
        parent: T.Overlay.overlay
        title: qsTr("Move to trash?")
        subject: Workspace.pageTitle(path)
        message: qsTr("It will be moved to the trash. You can restore it from there.")
        confirmText: qsTr("Move to trash")
        destructive: true
        onAccepted: {
            const trashPath = Workspace.deletePage(path)
            if (trashPath !== "") {
                // Restored by the controller: the toast outlives this view.
                AppController.toastRestorePage(qsTr("Moved to trash"), qsTr("Undo"), trashPath)
            }
        }
    }
}
