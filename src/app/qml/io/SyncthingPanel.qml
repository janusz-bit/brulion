pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// SyncthingPanel — sync through Syncthing: the notebook is a plain folder that Syncthing keeps the
// same on every device (desktop service, Syncthing-Fork on Android). Shows the steps (with the
// folder's path), the live status from Syncthing's REST API (Workspace.sync.syncthing: state,
// completion, connected devices; the folder is found by its path), the API address and key
// (SyncthingSetup; on a computer the key is read from Syncthing's own config) and the
// recommended .stignore. The browser version cannot use Syncthing: it says so.
ColumnLayout {
    id: panel

    readonly property var status: Workspace.sync ? Workspace.sync.syncthing : ({})
    readonly property bool shared: !!status.folder || (status.folderId || "") !== ""
    property bool showTemplate: false
    property bool showKey: false

    spacing: Theme.space4

    SyncthingSetup {
        id: setup
        settings: AppSettings
        workspace: Workspace
        onErrorOccurred: (message) => AppController.toast(message, "error")
    }

    // ---- Browser: not available ---------------------------------------------------------------
    IoCard {
        visible: AppController.isWasm
        iconName: "folder-sync"
        title: qsTr("Syncthing is not available in the browser")
        subtitle: qsTr("Syncthing syncs folders on a computer or a phone. Here, use Nextcloud – or the Brulion app for Linux or Android with Syncthing.")
    }

    // ---- Status --------------------------------------------------------------------------------
    IoCard {
        visible: !AppController.isWasm
        iconName: "folder-sync"
        iconColor: panel.status.state === "idle" ? Theme.successText
                 : panel.status.state === "offline" || panel.status.state === "not-shared" ? Theme.warningText
                 : panel.status.state === "error" ? Theme.dangerText : Theme.fgMuted
        title: "Syncthing"
        subtitle: panel.status.text || (panel.shared
                  ? qsTr("This notebook folder is shared with Syncthing.")
                  : qsTr("Syncthing keeps this folder the same on all your devices – directly between them, without a server."))
        trailing: [
            BBadge {
                visible: text !== ""
                text: {
                    switch (panel.status.state || "") {
                    case "idle": return qsTr("Up to date")
                    case "syncing":
                    case "sync-preparing": return qsTr("Syncing")
                    case "scanning": return qsTr("Scanning")
                    case "offline": return qsTr("Not running")
                    case "not-shared": return qsTr("Not shared")
                    case "error": return qsTr("Errors")
                    case "unknown": return qsTr("Checking…")
                    default: return panel.shared ? qsTr("Shared") : ""
                    }
                }
                variant: {
                    switch (panel.status.state || "") {
                    case "idle": return "success"
                    case "syncing":
                    case "sync-preparing":
                    case "scanning": return "info"
                    case "offline":
                    case "not-shared": return "warning"
                    case "error": return "danger"
                    default: return "neutral"
                    }
                }
            },
            BIconButton {
                visible: !!panel.status.enabled
                iconName: "refresh-cw"
                label: qsTr("Check now")
                size: "small"
                onClicked: Workspace.sync.refreshSyncthing()
            }
        ]

        BProgressBar {
            Layout.fillWidth: true
            visible: panel.status.state === "syncing" || panel.status.state === "sync-preparing"
            value: (panel.status.completion || 0) / 100
        }
        Flow {
            Layout.fillWidth: true
            visible: !!panel.status.enabled && panel.status.state !== "offline" && panel.status.state !== "not-shared"
                     && panel.status.state !== "unknown"
            spacing: Theme.space2
            BChip {
                visible: (panel.status.folderLabel || "") !== ""
                iconName: "folder"
                text: qsTr("Folder “%1”").arg(panel.status.folderLabel || "")
            }
            BChip {
                visible: panel.status.connected !== undefined && panel.status.connected >= 0
                iconName: "laptop"
                text: qsTr("%n device(s) connected", "", panel.status.connected || 0)
            }
            BChip {
                visible: panel.status.completion !== undefined
                iconName: "circle-check"
                text: qsTr("%1 % in sync").arg(Math.floor(panel.status.completion || 0))
            }
        }
        BText {
            Layout.fillWidth: true
            visible: (panel.status.error || "") !== "" && panel.status.state === "offline"
            text: panel.status.error || ""
            variant: "small"
            color: Theme.fgFaint
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        }
    }

    // ---- How to ---------------------------------------------------------------------------------
    IoCard {
        visible: !AppController.isWasm
        iconName: "list-checks"
        title: qsTr("How to set it up")
        contentSpacing: Theme.space4

        IoStep {
            number: 1
            text: qsTr("Install Syncthing on your computer (on NixOS: services.syncthing.enable = true) and open its panel at http://127.0.0.1:8384.")
        }
        IoStep {
            number: 2
            done: panel.shared
            text: qsTr("Add this notebook folder there (“Add Folder”) and share it with your other devices:")
            TextEdit {
                Layout.fillWidth: true
                readOnly: true
                selectByMouse: true
                text: Workspace.rootPath
                wrapMode: TextEdit.WrapAtWordBoundaryOrAnywhere
                font.family: Theme.fontMono
                font.pixelSize: Theme.textSm
                color: Theme.fg
                selectionColor: Theme.selection
                selectedTextColor: Theme.fg
                Accessible.role: Accessible.EditableText
                Accessible.name: qsTr("Notebook folder")
            }
        }
        IoStep {
            number: 3
            text: qsTr("On Android install Syncthing-Fork (F-Droid or Google Play), accept the shared folder and choose where it goes, for example /storage/emulated/0/Notatki.")
        }
        IoStep {
            number: 4
            text: qsTr("In Brulion on the phone open that same folder as the notebook (Menu → Open another notebook).")
        }
        IoStep {
            number: 5
            done: setup.stignoreState === "current"
            text: qsTr("Add the recommended .stignore on every device (below), so that Brulion's trash and temporary files stay on each device.")
        }
        BText {
            Layout.fillWidth: true
            text: qsTr("Edits made on two devices at the same time end up as conflict copies; Brulion helps you merge them in Conflicts.")
            variant: "small"
            color: Theme.fgMuted
            wrapMode: Text.Wrap
        }
    }

    // ---- .stignore ------------------------------------------------------------------------------
    IoCard {
        visible: !AppController.isWasm && setup.stignoreState !== "unavailable"
        iconName: "eye-off"
        title: qsTr("Files Syncthing leaves out (.stignore)")
        subtitle: setup.stignoreState === "current"
                  ? qsTr("The notebook has the recommended rules.")
                  : setup.stignoreState === "outdated"
                    ? qsTr("The notebook has its own .stignore without some of the recommended rules. Brulion adds them after yours.")
                    : qsTr("The notebook has no .stignore yet.")
        trailing: [
            BBadge {
                text: setup.stignoreState === "current" ? qsTr("OK") : setup.stignoreState === "outdated" ? qsTr("Incomplete") : qsTr("Missing")
                variant: setup.stignoreState === "current" ? "success" : "warning"
            }
        ]

        Flow {
            Layout.fillWidth: true
            spacing: Theme.space2
            BButton {
                visible: setup.stignoreState !== "current"
                text: setup.stignoreState === "outdated" ? qsTr("Add the missing rules") : qsTr("Create .stignore")
                iconName: "plus"
                variant: "primary"
                onClicked: {
                    if (setup.writeStignore())
                        AppController.toast(qsTr("Saved .stignore. Copy it to your other devices too – Syncthing does not sync it."), "success")
                }
            }
            BButton {
                text: panel.showTemplate ? qsTr("Hide the rules") : qsTr("Show the rules")
                iconName: panel.showTemplate ? "chevron-down" : "chevron-right"
                variant: "ghost"
                onClicked: panel.showTemplate = !panel.showTemplate
            }
        }
        Rectangle {
            Layout.fillWidth: true
            visible: panel.showTemplate
            implicitHeight: templateText.implicitHeight + 2 * Theme.space3
            radius: Theme.radius
            color: Theme.bgSunken
            BText {
                id: templateText
                anchors.fill: parent
                anchors.margins: Theme.space3
                text: setup.stignoreTemplate
                variant: "mono"
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            }
        }
        BText {
            Layout.fillWidth: true
            text: setup.stignorePath
            variant: "small"
            color: Theme.fgFaint
            elide: Text.ElideMiddle
        }
    }

    // ---- REST access ---------------------------------------------------------------------------
    IoCard {
        visible: !AppController.isWasm
        iconName: "settings"
        title: qsTr("Status from Syncthing")
        subtitle: AppController.isDesktop
                  ? qsTr("On a computer Brulion reads the API key from Syncthing's own settings. Fill it in only if the status does not appear.")
                  : qsTr("Copy the API key from Syncthing-Fork: Settings → Syncthing options → GUI → API key.")

        GridLayout {
            Layout.fillWidth: true
            columns: Theme.compact ? 1 : 2
            columnSpacing: Theme.space4
            rowSpacing: Theme.space3
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.space1
                BText { text: qsTr("Address"); variant: "label" }
                BTextField {
                    Layout.fillWidth: true
                    text: setup.url
                    placeholderText: setup.defaultUrl
                    accessibleName: qsTr("Syncthing address")
                    inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                    errorText: setup.urlError
                    onTextEdited: setup.url = text
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.space1
                BText { text: qsTr("API key"); variant: "label" }
                BTextField {
                    Layout.fillWidth: true
                    text: setup.apiKey
                    echoMode: panel.showKey ? TextInput.Normal : TextInput.Password
                    placeholderText: AppController.isDesktop ? qsTr("Found automatically") : qsTr("Paste the API key")
                    accessibleName: qsTr("Syncthing API key")
                    inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                    trailingItem: BIconButton {
                        iconName: panel.showKey ? "eye-off" : "eye"
                        label: panel.showKey ? qsTr("Hide the key") : qsTr("Show the key")
                        size: "small"
                        onClicked: panel.showKey = !panel.showKey
                    }
                    onTextEdited: setup.apiKey = text
                }
            }
        }
        RowLayout {
            spacing: Theme.space2
            BButton {
                text: qsTr("Save")
                iconName: "check"
                variant: "primary"
                enabled: setup.dirty
                onClicked: {
                    if (setup.save())
                        AppController.toast(qsTr("Saved. Brulion asks Syncthing again."), "success")
                }
            }
            BButton {
                visible: setup.dirty
                text: qsTr("Discard changes")
                variant: "ghost"
                onClicked: setup.reload()
            }
        }
    }
}
