pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// SyncConflictList — the conflict copies of the notebook (Workspace.sync.conflicts: Syncthing
// ".sync-conflict-" and Brulion ".conflict-" copies), newest information first per note: the
// note's title, its folder, where the other version came from and when.
//
//   SyncConflictList { currentCopy: view.copy; onActivated: (copy) => view.copy = copy }
ListView {
    id: list

    property string currentCopy

    signal activated(string copy)

    function describe(c) {
        const parts = []
        const folder = Workspace.folderOf(c.original)
        if (folder !== "")
            parts.push(folder)
        parts.push(c.source === "syncthing" ? "Syncthing" : "Nextcloud")
        if (c.device)
            parts.push(c.device)
        if (c.when)
            parts.push(c.when)
        return parts.join(" · ")
    }

    clip: true
    model: Workspace.sync ? Workspace.sync.conflicts : []
    boundsBehavior: Flickable.StopAtBounds
    keyNavigationEnabled: true
    T.ScrollBar.vertical: BScrollBar {}
    Accessible.role: Accessible.List
    Accessible.name: qsTr("Conflicts")

    delegate: BListItem {
        id: item
        required property var modelData
        required property int index
        width: ListView.view.width
        iconName: modelData.drawing ? "pen-tool" : modelData.text ? "file-diff" : "file"
        text: modelData.title
        subtitle: list.describe(modelData)
        selected: modelData.copy === list.currentCopy
        selectionBar: true
        onClicked: {
            list.currentIndex = index
            list.activated(modelData.copy)
        }
    }
}
