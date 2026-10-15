pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// SyncDeletionGuard — the mass-deletion guard of the Nextcloud sync (Workspace.sync.
// needsConfirmation): many files vanished on one side at once (an unmounted card, a wrong server
// folder, a wiped server), so the engine held the deletions back. Lists them (ListView: there can
// be thousands) and applies them only after an explicit confirmation with a check box;
// "Sync again" retries after the user fixed the cause.
IoCard {
    id: guard

    readonly property var sync: Workspace.sync
    readonly property var deletions: sync ? sync.pendingDeletions : []
    readonly property int localCount: deletions.filter(d => d.action === "delete-local").length
    readonly property int remoteCount: deletions.length - localCount

    visible: !!sync && sync.needsConfirmation
    tone: "warning"
    iconName: "triangle-alert"
    iconColor: Theme.warningText
    title: qsTr("Sync paused: %n file(s) would be deleted", "", deletions.length)
    subtitle: qsTr("Many files disappeared at once. So that nothing is lost by mistake – an unplugged memory card, a wrong folder on the server – Brulion waits for your decision.")

    BText {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        text: guard.localCount > 0 && guard.remoteCount > 0
              ? qsTr("%1 would be deleted on this device and %2 on the server.").arg(guard.localCount).arg(guard.remoteCount)
              : guard.localCount > 0 ? qsTr("They were deleted on the server and would be deleted on this device too.")
              : qsTr("They were deleted on this device and would be deleted on the server too.")
    }

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: Math.min(list.contentHeight, 8 * 36) + 2
        radius: Theme.radius
        color: Theme.bgElevated
        border.width: 1
        border.color: Theme.border
        ListView {
            id: list
            anchors.fill: parent
            anchors.margins: 1
            clip: true
            model: guard.deletions
            boundsBehavior: Flickable.StopAtBounds
            T.ScrollBar.vertical: BScrollBar {}
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Files that would be deleted")
            delegate: RowLayout {
                id: row
                required property var modelData
                width: ListView.view.width
                height: 36
                spacing: Theme.space2
                BIcon {
                    Layout.leftMargin: Theme.space3
                    name: row.modelData.action === "delete-local" ? "smartphone" : "server"
                    size: Theme.iconSizeSm
                    color: Theme.fgMuted
                }
                BText {
                    Layout.fillWidth: true
                    text: row.modelData.path
                    elide: Text.ElideMiddle
                }
                BText {
                    Layout.rightMargin: Theme.space3
                    visible: !Theme.compact
                    text: row.modelData.action === "delete-local" ? qsTr("here") : qsTr("on the server")
                    variant: "small"
                    color: Theme.fgMuted
                }
            }
        }
    }

    Flow {
        Layout.fillWidth: true
        spacing: Theme.space2
        BButton {
            text: qsTr("Delete %n file(s)", "", guard.deletions.length)
            iconName: "trash-2"
            variant: "danger"
            onClicked: confirm.open()
        }
        BButton {
            text: qsTr("Sync again")
            iconName: "refresh-cw"
            onClicked: guard.sync.syncNow()
        }
    }

    BConfirmDialog {
        id: confirm
        title: qsTr("Delete %n file(s)?", "", guard.deletions.length)
        message: qsTr("The files listed will be deleted on the other side as well. Files deleted on this device go to the trash; files deleted on the server go to its deleted files.")
        checkText: qsTr("Yes, these files should be deleted")
        confirmText: qsTr("Delete")
        destructive: true
        acceptEnabled: checked
        onAboutToShow: checked = false
        onAccepted: guard.sync.confirmDeletions()
    }
}
