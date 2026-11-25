pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// GitChangeRow — one changed file of GitChangesPanel's list (a row of CodeGit.changes or
// .turnChanges): the status letter in its colour (M modified, A added, D deleted, R renamed,
// U untracked, C conflicted), the file's name with its folder dimmed after it, +N −N lines.
// Names are plain text (BText): a file can be called anything.
//
//   delegate: GitChangeRow { width: ListView.view.width; selected: path === panel.selectedPath
//                            onActivated: (path) => panel.openDiff(path) }
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
    required property int index

    property bool selected: false

    signal activated(string path)

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

    objectName: "gitChange_" + index
    implicitHeight: Math.max(Theme.touchUi ? Theme.touchTarget : 0, Theme.listItemHeightDense)
    leftPadding: Theme.space3
    rightPadding: Theme.space3
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    Accessible.role: Accessible.ListItem
    Accessible.name: row.statusText + ": " + row.path
    onClicked: row.activated(row.path)
    Keys.onReturnPressed: row.activated(row.path)
    Keys.onEnterPressed: row.activated(row.path)

    contentItem: RowLayout {
        spacing: Theme.space2
        BText {
            objectName: "gitChangeLetter"
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
            font.weight: row.selected ? Theme.weightMedium : Theme.weightNormal
        }
        BText {
            objectName: "gitChangeDir"
            Layout.fillWidth: true
            visible: row.dir !== "" || row.oldPath !== ""
            text: row.oldPath !== "" ? qsTr("from %1").arg(row.oldPath) : row.dir
            variant: "faint"
            font.pixelSize: Theme.textSm
            elide: Text.ElideLeft
        }
        Item {
            Layout.fillWidth: true
            visible: row.dir === "" && row.oldPath === ""
        }
        BText {
            objectName: "gitChangeBinary"
            visible: row.binary
            text: qsTr("binary")
            variant: "faint"
            font.pixelSize: Theme.textXs
        }
        BText {
            objectName: "gitChangeAdditions"
            visible: row.additions > 0
            text: "+" + row.additions
            color: Theme.successText
            font.family: Theme.fontMono
            font.pixelSize: Theme.textXs
        }
        BText {
            objectName: "gitChangeDeletions"
            visible: row.deletions > 0
            text: "−" + row.deletions
            color: Theme.dangerText
            font.family: Theme.fontMono
            font.pixelSize: Theme.textXs
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
