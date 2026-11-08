pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// ShellSyncFooter — the sync status: where the notes are synced (Nextcloud status from the sync
// integration via AppController.syncStatus — or, for a notebook kept on GitHub without Nextcloud,
// GitHub's, with its own icon —, a Syncthing folder, this browser, this device only).
// A line with "Sync now" when a sync engine is available, or `compact`: the status icon alone
// (the sidebar's rail, ShellRail), its text as the tooltip. Clicking it opens the sync center.
T.ItemDelegate {
    id: control

    property real safeBottom: 0
    /// Only the icon, in a square button (the rail).
    property bool compact: false

    /// AppController.syncStatus: { state, text, icon? }.
    readonly property string statusState: AppController.routeText(AppController.syncStatus, "state")
    /// The icon of a status that is not Nextcloud's ("git-branch": a notebook kept on GitHub).
    readonly property string statusIcon: AppController.routeText(AppController.syncStatus, "icon")
    // Without a sync engine an error can still stop the sync (another notebook syncs with the
    // same server folder): shown as one.
    readonly property string syncState: AppController.syncAvailable ? (statusState !== "" ? statusState : "idle")
                                      : statusState === "error" ? "error" : "none"
    readonly property string statusText: {
        const text = AppController.routeText(AppController.syncStatus, "text")
        if (text !== "")
            return text
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
        case "idle": return control.statusIcon !== "" ? control.statusIcon : "cloud"
        case "error": return "cloud-off"
        case "offline": return "cloud-off"
        }
        if (Workspace.syncthingFolder)
            return "folder-sync"
        return AppController.isWasm ? "monitor" : "hard-drive"
    }

    readonly property int squareSize: Theme.touchUi ? Theme.touchTarget : Theme.controlHeight + 4

    implicitWidth: compact ? squareSize : Math.max(implicitBackgroundWidth, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: compact ? squareSize
                            : Math.max(Theme.touchUi ? 52 : 40, implicitContentHeight + topPadding) + safeBottom
    leftPadding: compact ? (squareSize - Theme.iconSize) / 2 : Theme.touchUi ? 16 : 14
    rightPadding: compact ? leftPadding : 6
    bottomPadding: compact ? 0 : safeBottom
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
            visible: !control.compact
            Layout.fillWidth: true
            text: control.statusText
            variant: "small"
            color: control.syncState === "error" ? Theme.dangerText : Theme.fgMuted
            elide: Text.ElideRight
        }
        BIconButton {
            visible: AppController.syncAvailable && !control.compact
            enabled: control.syncState !== "syncing"
            iconName: "refresh-cw"
            label: control.statusIcon === "git-branch" ? qsTr("Get the newest version from GitHub") : qsTr("Sync now")
            size: "small"
            onClicked: AppController.requestSync()
        }
    }

    background: Rectangle {
        radius: control.compact ? Theme.radius : 0
        color: control.pressed ? Theme.bgPressed : control.hovered ? Theme.bgHover : "transparent"
        Rectangle {
            visible: !control.compact
            width: parent.width
            height: 1
            color: Theme.border
        }
        BFocusRing {
            target: control
            visible: control.visualFocus
            inset: !control.compact
            targetRadius: control.compact ? Theme.radius : 0
        }
    }

    Loader {
        active: control.compact && (control.hovered || control.visualFocus)
        sourceComponent: BToolTip {
            parent: control
            text: control.statusText
            beside: true
            visible: (control.hovered && !control.down) || control.visualFocus
        }
    }
}
