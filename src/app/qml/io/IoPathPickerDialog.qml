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
    property list<string> folders: []
    property string query

    signal chosen(string path)

    /// The rows: [{ title, subtitle, icon, path }].
    function rows(): list<var> {
        if (!dialog.visible)
            return []
        if (dialog.mode === "page")
            return Workspace.switcherPages(dialog.query, 60)
        return IoLists.folderItems(dialog.query, dialog.folders, 100)
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
        TouchFlick.enabled: true
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(Math.max(contentHeight, Theme.listItemHeightTwoLine), 380)
        clip: true
        model: dialog.rows()
        keyNavigationEnabled: true
        boundsBehavior: Flickable.StopAtBounds
        T.ScrollBar.vertical: BScrollBar {}
        Accessible.role: Accessible.List
        Accessible.name: dialog.title
        delegate: BListItem {
            id: row
            // (A row's "icon" is the button's own property.)
            required property var modelData
            required property string title
            required subtitle
            required property string path
            width: ListView.view.width
            iconName: AppController.routeText(modelData, "icon")
            text: title
            onClicked: {
                dialog.chosen(row.path)
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
