pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// DeviceFolderPanel — a folder of the phone this notebook syncs with (docs/ANDROID.md "A folder
// picked in the system's dialog"): the notebook lives in the app's storage, Brulion keeps it in
// sync with a folder picked in the system's dialog — the folder Syncthing-Fork syncs, say —
// without "All files access" (the app for Google Play has none). Not linked: why and "Choose a
// folder…"; linked: the folder, the sync's state, "Sync now", another folder, stop. On Android, for
// a notebook in the app's storage only (SyncView's Syncthing tab, the settings' Sync section).
//
//   DeviceFolderPanel { Layout.fillWidth: true }
IoCard {
    id: panel

    readonly property WorkspaceSync sync: Workspace.sync
    readonly property bool linked: AppSettings.deviceFolderLinked
    readonly property bool active: sync !== null && sync.deviceFolder && sync.active
    readonly property string syncState: sync !== null && sync.deviceFolder ? sync.state : ""
    /// "Stop syncing" asked: the confirmation is shown in place.
    property bool confirming: false

    visible: AppController.deviceFoldersAvailable && Workspace.isOpen && AppController.inAppStorage(Workspace.rootPath)
    iconName: "folder-sync"
    iconColor: !linked ? Theme.fgMuted
             : syncState === "error" ? Theme.dangerText
             : syncState === "idle" ? Theme.successText : Theme.fgMuted
    tone: linked && syncState === "error" ? "danger" : "default"
    title: linked ? qsTr("Folder “%1” on this phone").arg(AppSettings.deviceFolderName) : qsTr("A folder on this phone")
    subtitle: linked
              ? qsTr("Brulion keeps this notebook in sync with the folder – within seconds of your edits, every few minutes and when you come back to the app.")
              : qsTr("Pick the folder Syncthing-Fork syncs (or any folder on the phone or a memory card). Brulion keeps this notebook in sync with it – no special permission needed.")
    trailing: [
        BBadge {
            visible: panel.linked && panel.syncState !== ""
            text: {
                switch (panel.syncState) {
                case "syncing": return qsTr("Syncing")
                case "error": return qsTr("Problem")
                default: return qsTr("On")
                }
            }
            variant: panel.syncState === "error" ? "danger" : panel.syncState === "syncing" ? "info" : "success"
        }
    ]

    BText {
        Layout.fillWidth: true
        visible: panel.linked && AppSettings.deviceFolderWhere !== ""
        text: qsTr("Where: %1").arg(AppSettings.deviceFolderWhere)
        variant: "small"
        color: Theme.fgMuted
        wrapMode: Text.WrapAnywhere
    }
    BText {
        Layout.fillWidth: true
        visible: panel.linked && text !== ""
        text: panel.sync !== null && panel.sync.deviceFolder ? panel.sync.statusText : ""
        variant: "small"
        color: panel.syncState === "error" ? Theme.dangerText : Theme.fgMuted
        wrapMode: Text.Wrap
    }

    Flow {
        Layout.fillWidth: true
        visible: !panel.confirming
        spacing: Theme.space2
        BButton {
            objectName: "deviceFolderChoose"
            visible: !panel.linked
            text: qsTr("Choose a folder…")
            iconName: "folder-open"
            variant: "primary"
            onClicked: folderDialog.open()
        }
        BButton {
            objectName: "deviceFolderSyncNow"
            visible: panel.active
            text: qsTr("Sync now")
            iconName: "refresh-cw"
            variant: "primary"
            busy: panel.syncState === "syncing"
            onClicked: panel.sync.syncNow()
        }
        BButton {
            objectName: "deviceFolderChange"
            visible: panel.linked
            text: qsTr("Choose another folder")
            iconName: "folder-open"
            onClicked: folderDialog.open()
        }
        BButton {
            objectName: "deviceFolderStop"
            visible: panel.linked
            text: qsTr("Stop syncing")
            iconName: "folder-x"
            variant: "ghost"
            onClicked: panel.confirming = true
        }
    }

    // ---- "Stop syncing": what happens, in place ------------------------------------------------
    BText {
        Layout.fillWidth: true
        visible: panel.confirming
        text: qsTr("The notes stay in this notebook and in the folder; changes on one side no longer reach the other.")
        wrapMode: Text.Wrap
    }
    Flow {
        Layout.fillWidth: true
        visible: panel.confirming
        spacing: Theme.space2
        BButton {
            objectName: "deviceFolderStopConfirm"
            text: qsTr("Stop syncing")
            variant: "danger"
            onClicked: {
                panel.confirming = false
                AppController.unlinkDeviceFolder()
            }
        }
        BButton {
            text: qsTr("Cancel")
            variant: "ghost"
            onClicked: panel.confirming = false
        }
    }

    FilePicker {
        id: folderDialog
        objectName: "deviceFolderDialog"
        title: qsTr("Choose the folder to sync with")
        fileMode: FilePicker.OpenFolder
        dark: Theme.dark
        colors: Theme.colors
        onAccepted: AppController.linkDeviceFolder(selectedFolder)
    }
}
