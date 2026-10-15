pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// IoPathPickerDialog — choose a folder or a page of the notebook (export scope), with a search
// field. Folders are ranked by TextHelper.fuzzyRank (at most 100 shown), pages come from the
// index (Workspace.switcherPages: recent pages first, at most 60) — never the whole list.
//
//   IoPathPickerDialog { id: picker; mode: "folder"; folders: exporter.folders
//                        onChosen: (path) => exporter.folder = path }
BDialog {
    id: dialog

    /// "folder" | "page"
    property string mode: "folder"
    property var folders: []
    property string query

    signal chosen(string path)

    readonly property var items: {
        if (!visible)
            return []
        if (mode === "page")
            return Workspace.switcherPages(query, 60)
        const list = folders || []
        return TextHelper.fuzzyRank(query, list, 100).map(r => ({
            title: list[r.index].split("/").pop(),
            subtitle: list[r.index].indexOf("/") >= 0 ? list[r.index].slice(0, list[r.index].lastIndexOf("/")) : "",
            icon: "folder",
            path: list[r.index]
        }))
    }

    title: mode === "folder" ? qsTr("Choose a folder") : qsTr("Choose a page")
    iconName: mode === "folder" ? "folder" : "file-text"
    showFooter: false
    initialFocusItem: search
    preferredWidth: Theme.dialogWidth
    onAboutToShow: {
        search.text = ""
        query = ""
    }

    BSearchField {
        id: search
        Layout.fillWidth: true
        placeholderText: dialog.mode === "folder" ? qsTr("Search folders…") : qsTr("Search pages…")
        onSearch: (text) => dialog.query = text
        Keys.onDownPressed: list.forceActiveFocus()
    }

    ListView {
        id: list
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(Math.max(contentHeight, Theme.listItemHeightTwoLine), 380)
        clip: true
        model: dialog.items
        keyNavigationEnabled: true
        boundsBehavior: Flickable.StopAtBounds
        T.ScrollBar.vertical: BScrollBar {}
        Accessible.role: Accessible.List
        Accessible.name: dialog.title
        delegate: BListItem {
            id: row
            required property var modelData
            width: ListView.view.width
            iconName: modelData.icon
            text: modelData.title
            subtitle: modelData.subtitle
            onClicked: {
                dialog.chosen(modelData.path)
                dialog.close()
            }
        }
        BText {
            anchors.centerIn: parent
            visible: list.count === 0
            text: dialog.mode === "folder" ? qsTr("No folders found.") : qsTr("No pages found.")
            variant: "muted"
        }
    }
}
