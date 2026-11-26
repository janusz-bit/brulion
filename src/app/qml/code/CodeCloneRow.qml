pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// CodeCloneRow — a repository being cloned, in CodeSidebar above the projects (docs/CODE.md
// "Cloning a repository"; a row of CodeProjects.clones): while git runs its name, git's stage and
// percent with a bar, and Cancel; after it failed the error, Retry and Dismiss. Done, it leaves the
// list and its folder is a project.
ColumnLayout {
    id: row

    required property var modelData
    readonly property string folder: AppController.routeText(row.modelData, "folder")
    readonly property string repositoryName: AppController.routeText(row.modelData, "name")
    readonly property bool failed: AppController.routeText(row.modelData, "phase") === "failed"
    readonly property string statusText: AppController.routeText(row.modelData, "status")
    readonly property int percent: AppController.routeInt(row.modelData, "percent", -1)

    objectName: "codeCloneRow"
    Layout.fillWidth: true
    Layout.leftMargin: Theme.space3
    Layout.rightMargin: Theme.space2
    spacing: Theme.space1

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.space2
        BIcon {
            name: row.failed ? "circle-alert" : "download"
            size: Theme.iconSizeSm
            color: row.failed ? Theme.dangerText : Theme.fgMuted
        }
        BText {
            Layout.fillWidth: true
            text: row.repositoryName
            elide: Text.ElideMiddle
        }
        BIconButton {
            objectName: "codeCloneRetry"
            visible: row.failed
            size: "small"
            iconName: "rotate-ccw"
            label: qsTr("Try the clone again")
            onClicked: Workspace.projects.retryClone(row.folder)
        }
        BIconButton {
            objectName: "codeCloneCancel"
            size: "small"
            iconName: "x"
            label: row.failed ? qsTr("Dismiss") : qsTr("Stop the clone")
            onClicked: Workspace.projects.cancelClone(row.folder)
        }
    }
    BProgressBar {
        visible: !row.failed
        Layout.fillWidth: true
        indeterminate: row.percent < 0
        value: row.percent < 0 ? 0 : row.percent
        to: 100
    }
    BText {
        objectName: "codeCloneStatus"
        Layout.fillWidth: true
        text: row.statusText
        variant: "small"
        color: row.failed ? Theme.dangerText : Theme.fgMuted
        wrapMode: row.failed ? Text.Wrap : Text.NoWrap
        elide: row.failed ? Text.ElideNone : Text.ElideRight
        maximumLineCount: row.failed ? 4 : 1
    }
}
