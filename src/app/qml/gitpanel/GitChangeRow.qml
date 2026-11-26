pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// GitChangeRow — one row of GitChangesPanel's file list (a row of CodeGit.changes / compareChanges,
// or of CodeGit.treeRows): a changed file — the status letter in its colour (M modified, A added,
// D deleted, R renamed, U untracked, C conflicted), the file's name with its folder dimmed after it
// (in the tree: indented under its folder), a dot when something of it is staged, +N −N lines — or,
// in the tree, a folder (a chevron to fold it, the number of files under it). A right click, a long
// press or the "⋯" button asks for the file's menu (menuRequested). Names are plain text (BText): a
// file can be called anything.
//
//   delegate: GitChangeRow { width: ListView.view.width; selected: path === panel.selectedPath
//                            onActivated: (path, oldPath) => panel.openDiff(path, oldPath)
//                            onFolderToggled: (path) => panel.toggleFolder(path)
//                            onMenuRequested: (target, x, y) => panel.openFileMenu(target, x, y) }
T.ItemDelegate {
    id: row

    required property string path
    required property string oldPath
    required property string status
    required property string letter
    required property string name
    required property string dir
    required property int additions
    required property int deletions
    required property bool binary
    required property bool staged
    required property bool unstaged
    required property string kind
    required property int depth
    required property bool collapsed
    required property int fileCount
    required property int index

    property bool selected: false
    /// The folder is shown after the name (the flat list); in the tree the indentation says it.
    property bool showDir: true
    /// The staged dot (the Uncommitted source only).
    property bool showStaged: false

    readonly property bool folder: row.kind === "folder"

    signal activated(string path, string oldPath)
    signal folderToggled(string path)
    /// The file's menu, at (x, y) in `target`.
    signal menuRequested(Item target, real x, real y)

    readonly property color letterColor: {
        switch (row.letter) {
        case "A":
        case "U":
            return Theme.successText
        case "D":
        case "C":
            return Theme.dangerText
        case "R":
            return Theme.infoText
        default:
            return Theme.warningText
        }
    }
    readonly property string statusText: {
        switch (row.status) {
        case "added":
            return qsTr("Added")
        case "deleted":
            return qsTr("Deleted")
        case "renamed":
            return qsTr("Renamed")
        case "untracked":
            return qsTr("New, untracked")
        case "conflicted":
            return qsTr("Conflict")
        default:
            return qsTr("Modified")
        }
    }

    function activate(): void {
        if (row.folder)
            row.folderToggled(row.path)
        else
            row.activated(row.path, row.oldPath)
    }

    objectName: (row.folder ? "gitFolder_" : "gitChange_") + index
    implicitHeight: Math.max(Theme.touchUi ? Theme.touchTarget : 0, Theme.listItemHeightDense)
    leftPadding: Theme.space3 + row.depth * Theme.space4
    rightPadding: Theme.space2
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    Accessible.role: row.folder ? Accessible.TreeItem : Accessible.ListItem
    Accessible.name: row.folder ? qsTr("Folder %1, %n file(s)", "", row.fileCount).arg(row.path)
                                : row.statusText + ": " + row.path
    onClicked: row.activate()
    onPressAndHold: {
        if (!row.folder)
            row.menuRequested(row, row.width / 2, row.height)
    }
    Keys.onReturnPressed: row.activate()
    Keys.onEnterPressed: row.activate()
    Keys.onMenuPressed: {
        if (!row.folder)
            row.menuRequested(row, row.width / 2, row.height)
    }

    TapHandler {
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        acceptedButtons: Qt.RightButton
        enabled: !row.folder
        onTapped: (point) => row.menuRequested(row, point.position.x, point.position.y)
    }

    contentItem: RowLayout {
        spacing: Theme.space2
        BIcon {
            objectName: "gitFolderChevron"
            visible: row.folder
            name: row.collapsed ? "chevron-right" : "chevron-down"
            size: Theme.iconSizeSm
            color: Theme.fgMuted
        }
        BText {
            objectName: "gitChangeLetter"
            visible: !row.folder
            Layout.preferredWidth: Theme.textBase
            horizontalAlignment: Text.AlignHCenter
            text: row.letter
            color: row.letterColor
            font.family: Theme.fontMono
            font.pixelSize: Theme.textSm
            font.weight: Theme.weightSemiBold
        }
        BText {
            objectName: "gitChangeName"
            // Its whole width while there is room; elided before the folder is gone entirely.
            Layout.minimumWidth: Math.min(implicitWidth, Theme.space16 * 2)
            Layout.preferredWidth: implicitWidth
            text: row.name
            elide: Text.ElideMiddle
            color: row.status === "deleted" ? Theme.fgMuted : Theme.fg
            font.strikeout: row.status === "deleted"
            font.weight: row.selected || row.folder ? Theme.weightMedium : Theme.weightNormal
        }
        BText {
            objectName: "gitChangeDir"
            Layout.fillWidth: true
            visible: !row.folder && ((row.showDir && row.dir !== "") || row.oldPath !== "")
            text: row.oldPath !== "" ? qsTr("from %1").arg(row.oldPath) : row.dir
            variant: "faint"
            font.pixelSize: Theme.textSm
            elide: Text.ElideLeft
        }
        Item {
            Layout.fillWidth: true
            visible: row.folder || ((!row.showDir || row.dir === "") && row.oldPath === "")
        }
        BText {
            objectName: "gitFolderCount"
            visible: row.folder
            text: "" + row.fileCount
            variant: "faint"
            font.pixelSize: Theme.textXs
        }
        Rectangle {
            objectName: "gitChangeStaged"
            visible: row.showStaged && row.staged && !row.folder
            implicitWidth: 6
            implicitHeight: 6
            radius: 3
            color: row.unstaged ? Theme.warning : Theme.success
            Accessible.role: Accessible.StaticText
            Accessible.name: row.unstaged ? qsTr("Partly staged") : qsTr("Staged")
        }
        BText {
            objectName: "gitChangeBinary"
            visible: row.binary && !row.folder
            text: qsTr("binary")
            variant: "faint"
            font.pixelSize: Theme.textXs
        }
        BText {
            objectName: "gitChangeAdditions"
            visible: row.additions > 0 && !row.folder
            text: "+" + row.additions
            color: Theme.successText
            font.family: Theme.fontMono
            font.pixelSize: Theme.textXs
        }
        BText {
            objectName: "gitChangeDeletions"
            visible: row.deletions > 0 && !row.folder
            text: "−" + row.deletions
            color: Theme.dangerText
            font.family: Theme.fontMono
            font.pixelSize: Theme.textXs
        }
        BIconButton {
            objectName: "gitChangeMore"
            visible: !row.folder && (row.hovered || row.selected || Theme.touchUi)
            iconName: "ellipsis"
            label: qsTr("Actions for %1").arg(row.name)
            size: "small"
            onClicked: row.menuRequested(row, row.width - width / 2, row.height)
        }
    }

    background: Rectangle {
        radius: Theme.radiusSm
        color: row.selected ? Theme.bgSelected : row.down ? Theme.bgPressed : row.hovered ? Theme.bgHover : "transparent"
        BFocusRing {
            visible: row.visualFocus
            targetRadius: Theme.radiusSm
        }
    }
}
