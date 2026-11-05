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

    /// "folder · Syncthing · device · when" (the parts there are).
    function describe(original: string, source: string, device: string, when: string): string {
        const folder = Workspace.folderOf(original)
        const from = source === "syncthing" ? "Syncthing" : "Nextcloud"
        let out = folder !== "" ? folder + " · " + from : from
        if (device !== "")
            out += " · " + device
        if (when !== "")
            out += " · " + when
        return out
    }

    clip: true
    model: Workspace.sync.conflicts
    boundsBehavior: Flickable.StopAtBounds
    keyNavigationEnabled: true
    T.ScrollBar.vertical: BScrollBar {}
    Accessible.role: Accessible.List
    Accessible.name: qsTr("Conflicts")

    delegate: BListItem {
        id: item
        required property string original
        required property string copy
        required property string title
        required property string source
        required property string device
        required property string when
        required property bool drawing
        // The row itself, for its "text" (a text file that can be merged; BListItem has a `text`
        // of its own).
        required property var modelData
        required property int index
        readonly property bool textFile: AppController.routeFlag(modelData, "text")
        width: ListView.view.width
        iconName: drawing ? "pen-tool" : textFile ? "file-diff" : "file"
        text: title
        subtitle: list.describe(original, source, device, when)
        selected: copy === list.currentCopy
        selectionBar: true
        onClicked: {
            list.currentIndex = index
            list.activated(item.copy)
        }
    }
}
