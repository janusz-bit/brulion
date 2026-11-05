pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// ShellBackupSettings — the "Kopie zapasowe" section of SettingsView (docs/UI.md "Backups"; C++:
// Workspace.backups, shell/workspacebackups.h): when the last backup was made and any problem with
// it, the schedule, how many are kept, the folder (the browser has none: its copies live in its own
// storage, and it says so), "Utwórz kopię teraz", and the backups in the folder, each with Przywróć,
// Usuń and — per platform — Pobierz (the browser) or Udostępnij (Android's private folder). A restore
// shows what it would do as counts first (Workspace.backups.prepareRestore → restorePlanReady).
ColumnLayout {
    id: section

    readonly property WorkspaceBackups backups: Workspace.backups
    /// What restorePlanReady delivered, shown by the confirmation.
    property var plan: ({})
    /// The backup a delete confirmation is about.
    property string deleting: ""
    /// The confirmed delete is of a foreign backup.
    property bool deletingForeign: false
    /// The foreign backup a restore would start from, waiting for its own confirmation.
    property string foreignRestore: ""

    function runningText(): string {
        switch (section.backups.phase) {
        case "backup": return qsTr("Creating a backup…")
        case "check": return qsTr("Checking the backup…")
        case "sync": return qsTr("Waiting for the running sync to finish…")
        case "safety": return qsTr("Saving the notebook as it is now…")
        case "restore": return qsTr("Restoring…")
        default: return ""
        }
    }

    Layout.fillWidth: true
    spacing: Theme.space2

    Component.onCompleted: section.backups.refresh()

    Connections {
        target: section.backups
        function onRestorePlanReady(plan: var) {
            if (!AppController.routeFlag(plan, "hasChanges")) {
                section.backups.dismissRestore()
                AppController.toast(qsTr("This notebook already holds exactly what the backup holds."), "info")
                return
            }
            section.plan = plan
            restoreDialog.open()
        }
    }

    BText {
        Layout.fillWidth: true
        text: qsTr("Backups")
        variant: "heading"
        wrapMode: Text.Wrap
        Accessible.role: Accessible.Heading
    }
    BText {
        Layout.fillWidth: true
        objectName: "backupsNote"
        wrapMode: Text.Wrap
        variant: "muted"
        text: !section.backups.available
              ? qsTr("The documentation needs no backups.")
              : section.backups.browser
                ? qsTr("Copies of this notebook as ZIP archives. They live in this browser: clearing its data removes them together with the notes. Download one to keep it elsewhere.")
                : qsTr("Copies of this notebook as ZIP archives. Sync does not spread them, so a copy in this device's own folder does not protect against losing the device.")
    }

    BCard {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space1
        visible: section.backups.available
        flat: Theme.compact
        padding: Theme.space5
        contentWidth: rows.implicitWidth
        contentHeight: rows.implicitHeight

        ColumnLayout {
            id: rows
            anchors.fill: parent
            spacing: Theme.compact ? Theme.space6 : Theme.space8

            ShellSettingRow {
                objectName: "backupsStatus"
                title: qsTr("Last backup")
                description: section.backups.lastBackupText !== "" ? section.backups.lastBackupText : qsTr("None yet.")
                BButton {
                    objectName: "backupsCreateNow"
                    text: qsTr("Create a backup now")
                    iconName: "hard-drive"
                    enabled: !section.backups.running
                    onClicked: section.backups.createNow()
                }
            }
            BText {
                objectName: "backupsProblem"
                Layout.fillWidth: true
                visible: text !== ""
                text: section.backups.statusText
                wrapMode: Text.Wrap
                color: section.backups.status === "failed" ? Theme.dangerText : Theme.fg
            }
            BButton {
                objectName: "backupsDismissInterrupted"
                visible: section.backups.interrupted
                text: qsTr("I have dealt with it")
                size: "small"
                variant: "ghost"
                onClicked: section.backups.dismissInterrupted()
            }
            ColumnLayout {
                objectName: "backupsProgress"
                visible: section.backups.running
                Layout.fillWidth: true
                spacing: Theme.space2
                RowLayout {
                    Layout.fillWidth: true
                    BText {
                        Layout.fillWidth: true
                        text: section.runningText()
                        variant: "muted"
                        wrapMode: Text.Wrap
                    }
                    BButton {
                        text: qsTr("Stop")
                        size: "small"
                        variant: "ghost"
                        onClicked: section.backups.cancel()
                    }
                }
                BProgressBar {
                    Layout.fillWidth: true
                    indeterminate: section.backups.progress <= 0
                    value: section.backups.progress
                }
            }
            BSwitch {
                objectName: "backupsEnabled"
                Layout.fillWidth: true
                text: qsTr("Automatic backups")
                description: qsTr("Checked when the notebook opens and every hour while Brulion runs. Nothing is written when nothing changed.")
                iconName: "hard-drive"
                checked: section.backups.enabled
                onToggled: section.backups.enabled = checked
            }
            ShellSettingRow {
                title: qsTr("How often")
                BSegmented {
                    objectName: "backupsInterval"
                    accessibleName: qsTr("How often")
                    model: [{ value: "daily", text: qsTr("Every day") }, { value: "weekly", text: qsTr("Every week") }]
                    currentValue: section.backups.interval
                    onActivated: (value) => section.backups.interval = value
                }
            }
            ShellSettingRow {
                title: qsTr("How many to keep")
                description: qsTr("The oldest go after a new backup has been made.")
                BSegmented {
                    objectName: "backupsKeep"
                    accessibleName: qsTr("How many to keep")
                    model: section.backups.keepChoices
                    currentValue: section.backups.keep
                    onActivated: (value) => section.backups.keep = value
                }
            }
            ShellSettingRow {
                objectName: "backupsFolderRow"
                visible: !section.backups.browser
                title: section.backups.usingDefaultFolder ? qsTr("Folder: this device's own") : qsTr("Folder")
                description: section.backups.folder
                BButton {
                    objectName: "backupsChooseFolder"
                    text: qsTr("Choose…")
                    iconName: "folder-open"
                    size: "small"
                    variant: "ghost"
                    enabled: !section.backups.running
                    onClicked: folderPicker.open()
                }
                BButton {
                    objectName: "backupsDefaultFolder"
                    visible: !section.backups.usingDefaultFolder
                    text: qsTr("Use the default")
                    size: "small"
                    variant: "ghost"
                    enabled: !section.backups.running
                    onClicked: section.backups.useDefaultFolder()
                }
                BButton {
                    objectName: "backupsOpenFolder"
                    visible: section.backups.canOpenFolder
                    text: qsTr("Open")
                    size: "small"
                    variant: "ghost"
                    onClicked: section.backups.openFolder()
                }
            }
        }
    }

    BText {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space4
        visible: section.backups.available
        text: qsTr("Saved backups")
        variant: "label"
        wrapMode: Text.Wrap
        Accessible.role: Accessible.Heading
    }
    BText {
        objectName: "backupsEmpty"
        Layout.fillWidth: true
        visible: section.backups.available && section.backups.backups.length === 0
        text: qsTr("No backups in this folder yet.")
        variant: "muted"
        wrapMode: Text.Wrap
    }
    ColumnLayout {
        objectName: "backupsList"
        Layout.fillWidth: true
        visible: section.backups.available
        spacing: 0

        Repeater {
            model: section.backups.backups

            BListItem {
                id: row
                // (WorkspaceBackups.backups' rows.)
                required property string name
                required property string when
                required property string sizeText
                required property bool foreign
                required property bool complete
                Layout.fillWidth: true
                objectName: "backupRow"
                iconName: "hard-drive"
                text: when
                subtitle: sizeText + (foreign ? (sizeText !== "" ? " · " : "") + qsTr("not from this notebook") : "")
                          + (!foreign && !complete ? (sizeText !== "" ? " · " : "") + qsTr("incomplete") : "")
                Accessible.description: name
                trailing: RowLayout {
                    spacing: 0
                    BIconButton {
                        objectName: "backupRestore"
                        size: "small"
                        variant: "ghost"
                        iconName: "rotate-ccw"
                        label: qsTr("Restore the backup from %1").arg(row.when)
                        enabled: !section.backups.running
                        onClicked: {
                            if (row.foreign) {
                                section.foreignRestore = row.name
                                foreignRestoreDialog.open()
                            } else {
                                section.backups.prepareRestore(row.name)
                            }
                        }
                    }
                    BIconButton {
                        objectName: "backupExport"
                        visible: section.backups.browser || section.backups.canShare
                        size: "small"
                        variant: "ghost"
                        iconName: section.backups.browser ? "download" : "share"
                        label: section.backups.browser ? qsTr("Download the backup from %1").arg(row.when)
                                                       : qsTr("Share the backup from %1").arg(row.when)
                        onClicked: section.backups.exportBackup(row.name)
                    }
                    BIconButton {
                        objectName: "backupDelete"
                        size: "small"
                        variant: "ghost"
                        iconName: "trash-2"
                        label: qsTr("Delete the backup from %1").arg(row.when)
                        enabled: !section.backups.running
                        onClicked: {
                            section.deleting = row.name
                            section.deletingForeign = row.foreign
                            deleteDialog.message = row.foreign
                                    ? qsTr("The backup from %1 was not made from this notebook: it may be the only copy of another one. It is deleted for good; it does not go to the trash.").arg(row.when)
                                    : qsTr("The backup from %1 is deleted for good; it does not go to the trash.").arg(row.when)
                            deleteDialog.open()
                        }
                    }
                }
            }
        }
    }

    FilePicker {
        id: folderPicker
        objectName: "backupsFolderDialog"
        title: qsTr("Choose a folder for backups")
        fileMode: FilePicker.OpenFolder
        dark: Theme.dark
        colors: Theme.colors
        onAccepted: section.backups.chooseFolder(selectedFolder)
    }

    BConfirmDialog {
        id: restoreDialog
        objectName: "backupsRestoreConfirm"
        parent: T.Overlay.overlay
        title: qsTr("Restore the backup from %1?").arg(AppController.routeText(section.plan, "when"))
        message: qsTr("%n file(s) will be replaced by the backup's version.", "", AppController.routeInt(section.plan, "changed", 0))
                 + " " + qsTr("%n file(s) will come back.", "", AppController.routeInt(section.plan, "recreated", 0))
                 + " " + qsTr("%n file(s) the backup does not hold will go to the trash.", "", AppController.routeInt(section.plan, "trashed", 0))
        detail: (AppController.routeFlag(section.plan, "incomplete") ? qsTr("This backup is not known to be complete, so files it does not hold stay where they are.") + " " : "")
                + (AppController.routeInt(section.plan, "tooLarge", 0) > 0
                   ? qsTr("%n file(s) are too large to restore and stay as they are.", "", AppController.routeInt(section.plan, "tooLarge", 0)) + " " : "")
                + qsTr("A safety backup of the notebook as it is now is made first. Replaced notes stay in the version history as well; other files only in the safety backup.")
        confirmText: qsTr("Restore")
        destructive: true
        onAccepted: section.backups.confirmRestore()
        onRejected: section.backups.dismissRestore()
    }

    BConfirmDialog {
        id: deleteDialog
        objectName: "backupsDeleteConfirm"
        parent: T.Overlay.overlay
        title: qsTr("Delete this backup?")
        confirmText: qsTr("Delete")
        destructive: true
        onAccepted: section.backups.remove(section.deleting, section.deletingForeign)
    }

    BConfirmDialog {
        id: foreignRestoreDialog
        objectName: "backupsForeignRestoreConfirm"
        parent: T.Overlay.overlay
        title: qsTr("Use a backup from another notebook?")
        message: qsTr("This backup was not made from this notebook as it is now: it comes from another notebook of the same name, or from this one when it lay in another folder. Restoring it may replace this notebook's notes with another notebook's.")
        confirmText: qsTr("Continue")
        destructive: true
        onAccepted: section.backups.prepareRestore(section.foreignRestore, true)
    }
}
