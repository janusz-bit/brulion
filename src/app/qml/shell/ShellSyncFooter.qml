pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// ShellSyncFooter — the sidebar's sync status line: where the notes are synced (Nextcloud
// status from the sync integration via AppController.syncStatus, a Syncthing folder, this
// browser, this device only) with "Sync now" when a sync engine is available. Clicking it opens
// the sync center.
T.ItemDelegate {
    id: control

    property real safeBottom: 0

    readonly property var status: AppController.syncStatus
    // Without a sync engine an error can still stop the sync (another notebook syncs with the
    // same server folder): shown as one.
    readonly property string syncState: AppController.syncAvailable ? (status.state || "idle")
                                      : status.state === "error" ? "error" : "none"
    readonly property string statusText: {
        if (status.text)
            return status.text
        switch (control.syncState) {
        case "idle": return qsTr("Synced with Nextcloud")
        case "syncing": return qsTr("Syncing…")
        case "error": return qsTr("Sync failed")
        case "offline": return qsTr("Offline — will sync later")
        }
        if (Workspace.syncthingFolder)
            return qsTr("Syncthing folder")
        if (AppController.isWasm)
            return qsTr("Stored in this browser")
        return qsTr("Only on this device")
    }
    readonly property string iconName: {
        switch (control.syncState) {
        case "idle": return "cloud"
        case "error": return "cloud-off"
        case "offline": return "cloud-off"
        }
        if (Workspace.syncthingFolder)
            return "folder-sync"
        return AppController.isWasm ? "monitor" : "hard-drive"
    }

    implicitWidth: Math.max(implicitBackgroundWidth, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(Theme.touchUi ? 52 : 40, implicitContentHeight + topPadding) + safeBottom
    leftPadding: Theme.touchUi ? 16 : 14
    rightPadding: 6
    bottomPadding: safeBottom
    hoverEnabled: !Theme.touchUi
    focusPolicy: Qt.StrongFocus

    Accessible.role: Accessible.Button
    Accessible.name: qsTr("Sync: %1").arg(statusText)

    contentItem: RowLayout {
        spacing: Theme.space2

        Item {
            Layout.preferredWidth: Theme.iconSize
            Layout.preferredHeight: Theme.iconSize
            BIcon {
                anchors.centerIn: parent
                visible: control.syncState !== "syncing"
                name: control.iconName
                size: Theme.iconSizeSm
                color: control.syncState === "error" ? Theme.dangerText : Theme.fgMuted
            }
            BSpinner {
                anchors.centerIn: parent
                visible: control.syncState === "syncing"
                size: Theme.iconSizeSm
            }
        }
        BText {
            Layout.fillWidth: true
            text: control.statusText
            variant: "small"
            color: control.syncState === "error" ? Theme.dangerText : Theme.fgMuted
            elide: Text.ElideRight
        }
        BIconButton {
            visible: AppController.syncAvailable
            enabled: control.syncState !== "syncing"
            iconName: "refresh-cw"
            label: qsTr("Sync now")
            size: "small"
            onClicked: AppController.requestSync()
        }
    }

    background: Rectangle {
        color: control.pressed ? Theme.bgPressed : control.hovered ? Theme.bgHover : "transparent"
        Rectangle {
            width: parent.width
            height: 1
            color: Theme.border
        }
        BFocusRing {
            target: control
            visible: control.visualFocus
            inset: true
            targetRadius: 0
        }
    }
}
