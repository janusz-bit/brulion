pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// SyncSettings — the "Synchronizacja" section of SettingsView: the state of both ways at a glance
// (Syncthing, Nextcloud with "Synchronizuj teraz", waiting conflicts), the way to the sync center,
// and the Nextcloud account form itself.
//
//   SyncSettings { Layout.fillWidth: true }
ColumnLayout {
    id: section

    readonly property var sync: Workspace.sync

    Layout.fillWidth: true
    spacing: Theme.space2

    BText {
        Layout.fillWidth: true
        text: qsTr("Sync")
        variant: "heading"
        wrapMode: Text.Wrap
        Accessible.role: Accessible.Heading
    }
    BText {
        Layout.fillWidth: true
        text: qsTr("Keep this notebook the same on all your devices – through Syncthing or your own Nextcloud.")
        variant: "muted"
        wrapMode: Text.Wrap
    }

    BCard {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space1
        padding: Theme.compact ? Theme.space4 : Theme.space5
        // Laid out like SettingsView's groups: rows divided by hairlines.
        contentWidth: syncRows.implicitWidth
        contentHeight: syncRows.implicitHeight
        ColumnLayout {
            id: syncRows
            anchors.fill: parent
            spacing: Theme.compact ? Theme.space6 : Theme.space8

            ShellSettingRow {
                visible: !AppController.isWasm
                title: "Syncthing"
                description: section.sync && section.sync.syncthing.text ? section.sync.syncthing.text
                             : Workspace.syncthingFolder ? qsTr("This notebook folder is shared with Syncthing.")
                             : qsTr("Not shared. Syncthing keeps a folder the same on all devices, without a server.")
                BBadge {
                    visible: Workspace.syncthingFolder
                    text: qsTr("Active")
                    variant: "success"
                }
            }
            ShellSettingRow {
                title: "Nextcloud"
                description: section.sync && section.sync.statusText !== "" ? section.sync.statusText
                             : qsTr("Not set up. Fill in the account below.")
                BButton {
                    visible: !!section.sync && section.sync.active
                    text: qsTr("Sync now")
                    iconName: "refresh-cw"
                    size: "small"
                    busy: !!section.sync && section.sync.state === "syncing"
                    onClicked: section.sync.syncNow()
                }
            }
            ShellSettingRow {
                visible: !!section.sync && section.sync.conflictCount > 0
                title: qsTr("Conflicts")
                description: qsTr("%n note(s) changed on two devices at once.", "", section.sync ? section.sync.conflictCount : 0)
                BButton {
                    text: qsTr("Merge")
                    iconName: "git-merge"
                    size: "small"
                    onClicked: AppController.navigate({ view: "sync", tab: "conflicts" })
                }
            }
            ShellSettingRow {
                title: qsTr("Event log")
                description: Logs.problemCount > 0
                             ? qsTr("%n warning(s) or error(s) since the app started. Every step of the sync, to check or to send.", "", Logs.problemCount)
                             : qsTr("Every step of the sync, to check or to send.")
                BButton {
                    text: qsTr("Open")
                    iconName: "scroll-text"
                    size: "small"
                    onClicked: AppController.navigate({ view: "logs", group: "sync" })
                }
            }
            ShellSettingRow {
                title: qsTr("Sync center")
                description: qsTr("Status, Syncthing setup, conflicts and merging.")
                BButton {
                    text: qsTr("Open")
                    iconName: "folder-sync"
                    variant: "primary"
                    onClicked: AppController.navigate({ view: "sync" })
                }
            }
        }
        ShellSettingDividers { column: syncRows }
    }

    SyncDeletionGuard {
        Layout.topMargin: Theme.space2
    }

    NextcloudForm {
        Layout.topMargin: Theme.space2
    }
}
