pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// SyncEncryptionCard — end-to-end encryption of the notebook's Nextcloud folder
// (docs/SYNC-ENCRYPTION.md; Workspace.sync.setUpEncryption): off, a server folder and a password
// turn it on — a new, empty folder becomes an encrypted notebook and its recovery key is shown
// once; a folder another device encrypted is opened with its password or recovery key. On, it
// says so; locked (encrypted, no key on this device), it asks for the password. The saved
// account is used (NextcloudForm saves it first). Used by SyncView and the settings' sync section.
//
//   SyncEncryptionCard { Layout.fillWidth: true }
IoCard {
    id: card

    readonly property bool encrypted: AppSettings.nextcloudEncrypted
    readonly property bool hasKey: AppSettings.hasNextcloudNotebookKey
    readonly property bool busy: Workspace.sync.encryptionBusy
    property bool showSecret: false

    iconName: "lock"
    title: qsTr("End-to-end encryption")
    subtitle: qsTr("The server keeps only encrypted names and files: without the notebook's password nobody can read them there, not even the server's owner. On this device the notes stay plain Markdown files.")
    contentSpacing: Theme.space3
    visible: AppSettings.nextcloudEnabled

    function proposedFolder(): string {
        return qsTr("%1 (encrypted)").arg(AppSettings.nextcloudFolder !== "" ? AppSettings.nextcloudFolder : "Brulion")
    }

    function start(): void {
        Workspace.sync.setUpEncryption(AppSettings.nextcloudUrl, AppSettings.nextcloudUser, "",
                                       card.encrypted ? AppSettings.nextcloudFolder : folderField.text,
                                       secretField.text)
    }

    Connections {
        target: Workspace.sync
        function onEncryptionReady(recoveryKey: string): void {
            secretField.text = ""
            if (recoveryKey !== "") {
                recoveryDialog.recoveryKey = recoveryKey
                recoveryDialog.open()
            } else {
                AppController.toast(qsTr("The encrypted notebook is open on this device."), "success")
            }
        }
        function onEncryptionPasswordChanged(): void {
            AppController.toast(qsTr("The notebook's password is changed. The recovery key stays the same."), "success")
        }
        function onEncryptionFailed(message: string): void {
            AppController.toast(message, "error")
        }
    }

    BText {
        Layout.fillWidth: true
        visible: card.encrypted && card.hasKey
        text: qsTr("On: the folder “%1” on the server is encrypted, and this device holds its key.").arg(AppSettings.nextcloudFolder)
        wrapMode: Text.Wrap
        color: Theme.successText
    }
    BText {
        Layout.fillWidth: true
        visible: card.encrypted && !card.hasKey
        text: qsTr("The folder “%1” on the server is encrypted. Enter its password (or its recovery key) to sync this notebook here.").arg(AppSettings.nextcloudFolder)
        wrapMode: Text.Wrap
        color: Theme.warningText
    }
    BText {
        Layout.fillWidth: true
        visible: !card.encrypted
        text: qsTr("It goes into a new, empty folder: the plain folder stays as it is until you delete it on the server. Every device needs Brulion 0.33 or later and the password. A forgotten password cannot be recovered without the recovery key — your notes on your devices stay readable either way.")
        variant: "small"
        color: Theme.fgMuted
        wrapMode: Text.Wrap
    }

    BTextField {
        id: folderField
        Layout.fillWidth: true
        visible: !card.encrypted
        text: card.proposedFolder()
        accessibleName: qsTr("Encrypted folder on the server")
        leadingIcon: "folder"
        inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
    }
    BTextField {
        id: secretField
        Layout.fillWidth: true
        visible: !(card.encrypted && card.hasKey)
        echoMode: card.showSecret ? TextInput.Normal : TextInput.Password
        placeholderText: card.encrypted ? qsTr("Password or recovery key") : qsTr("The notebook's password (at least 12 characters)")
        accessibleName: qsTr("The notebook's password")
        leadingIcon: "key-round"
        inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText | Qt.ImhHiddenText
        trailingItem: BIconButton {
            iconName: card.showSecret ? "eye-off" : "eye"
            label: card.showSecret ? qsTr("Hide the password") : qsTr("Show the password")
            size: "small"
            onClicked: card.showSecret = !card.showSecret
        }
        onAccepted: card.start()
    }
    BButton {
        visible: !(card.encrypted && card.hasKey)
        text: card.encrypted ? qsTr("Open the encrypted notebook") : qsTr("Encrypt on the server")
        iconName: card.encrypted ? "lock-open" : "lock"
        variant: "primary"
        busy: card.busy
        enabled: !card.busy && secretField.text !== ""
        onClicked: card.start()
    }

    BButton {
        visible: card.encrypted && card.hasKey
        text: qsTr("Change the password…")
        iconName: "key-round"
        size: "small"
        busy: card.busy
        onClicked: passwordDialog.open()
    }

    BDialog {
        id: passwordDialog
        title: qsTr("A new password for the notebook")
        iconName: "key-round"
        acceptText: qsTr("Change the password")
        acceptEnabled: newPassword.text.length >= 12 && newPassword.text === repeatedPassword.text
        initialFocusItem: newPassword
        onAccepted: Workspace.sync.changeEncryptionPassword(newPassword.text)
        onClosed: {
            newPassword.text = ""
            repeatedPassword.text = ""
        }
        BText {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: qsTr("Every device then opens the notebook with the new password; the ones that already hold its key go on syncing. The recovery key stays the same.")
        }
        BTextField {
            id: newPassword
            Layout.fillWidth: true
            echoMode: TextInput.Password
            placeholderText: qsTr("New password (at least 12 characters)")
            accessibleName: qsTr("New password")
            inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText | Qt.ImhHiddenText
        }
        BTextField {
            id: repeatedPassword
            Layout.fillWidth: true
            echoMode: TextInput.Password
            placeholderText: qsTr("The new password again")
            accessibleName: qsTr("The new password again")
            inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText | Qt.ImhHiddenText
            errorText: text !== "" && text !== newPassword.text ? qsTr("The two passwords differ.") : ""
        }
    }

    BDialog {
        id: recoveryDialog
        property string recoveryKey
        title: qsTr("Your recovery key")
        iconName: "key-round"
        acceptText: qsTr("I have written it down")
        rejectText: qsTr("Close")
        BText {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: qsTr("It opens the encrypted notebook when the password is forgotten. It is shown only now: write it down or print it, and keep it away from this device.")
        }
        TextEdit {
            id: recoveryText
            Layout.fillWidth: true
            readOnly: true
            selectByMouse: true
            textFormat: TextEdit.PlainText
            wrapMode: TextEdit.WrapAnywhere
            text: recoveryDialog.recoveryKey
            font.family: Theme.fontMono
            font.pixelSize: Theme.textLg
            color: Theme.fg
            Accessible.name: qsTr("Recovery key")
        }
        BButton {
            text: qsTr("Copy")
            iconName: "copy"
            size: "small"
            onClicked: {
                recoveryText.selectAll()
                recoveryText.copy()
                recoveryText.deselect()
                AppController.toast(qsTr("Copied. Clear the clipboard once you have kept it."), "info")
            }
        }
        onClosed: recoveryKey = ""
    }
}
