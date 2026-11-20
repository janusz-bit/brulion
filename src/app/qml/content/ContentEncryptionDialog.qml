pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ContentEncryptionDialog — a whole page's encryption (docs/UI.md "Encrypted notes";
// Workspace.encryption): "encrypt" ("Encrypt page…": the Markdown page becomes `<name>.mdenc`, every
// link following, its version history — the unencrypted versions — deleted unless unticked),
// "password" ("Change password…" of an unlocked encrypted page; its older versions, which open with
// the old password, deleted unless unticked), "new" ("New encrypted page…": a name, in `folder`).
// The password twice and a public hint; the work is asynchronous (a key derivation), the dialog
// stays open with a spinner until it answered. done(path) follows a success (the page to open).
//
//   ContentEncryptionDialog { onDone: (path) => AppController.replaceRoute({ view: "page", path: path }) }
//   dialog.openFor("encrypt", page.path)    dialog.openFor("new", folder)
BDialog {
    id: dialog
    objectName: "encryptionDialog"

    /// "encrypt" | "password" | "new"
    property string mode: "encrypt"
    /// The page ("encrypt", "password") or the folder of the new page ("new").
    property string path
    property int request: -1
    property bool working: false
    property string error

    signal done(string path)
    /// The work starts now (`mode`): an editor showing the page lets it go first.
    signal starting(string mode)
    /// It failed (the error is shown in the dialog, which stays open).
    signal failed()

    function openFor(kind: string, target: string): void {
        dialog.mode = kind
        dialog.path = target
        dialog.request = -1
        dialog.working = false
        dialog.error = ""
        passwordField.text = ""
        confirmField.text = ""
        nameField.text = ""
        historyBox.checked = true
        if (kind === "password") {
            info.map = Workspace.encryption.pageInfo(target)
            hintField.text = AppController.routeText(info.map, "hint")
        } else {
            hintField.text = ""
        }
        dialog.open()
    }
    MapHolder { id: info }

    readonly property bool passwordsMatch: passwordField.text === confirmField.text

    title: dialog.mode === "new" ? qsTr("New encrypted page")
           : dialog.mode === "password" ? qsTr("Change password") : qsTr("Encrypt page")
    subtitle: dialog.mode === "new" ? "" : Workspace.encryption.pageName(dialog.path)
    iconName: dialog.mode === "password" ? "key-round" : "lock"
    busy: dialog.working
    preferredWidth: Theme.dialogWidthSm
    initialFocusItem: dialog.mode === "new" ? nameField : passwordField
    acceptText: dialog.mode === "new" ? qsTr("Create") : dialog.mode === "password" ? qsTr("Change password") : qsTr("Encrypt")
    acceptEnabled: !dialog.working && passwordField.typedText !== "" && dialog.passwordsMatch
                   && Workspace.encryption.validHint(hintField.text)
                   && (dialog.mode !== "new" || Js.trim(nameField.text) !== "")
    onAcceptRequested: dialog.start()
    onClosed: {
        passwordField.text = ""
        confirmField.text = ""
    }

    function start(): void {
        if (dialog.working)
            return
        dialog.error = ""
        dialog.working = true
        dialog.starting(dialog.mode)
        if (dialog.mode === "encrypt")
            dialog.request = Workspace.encryption.encryptPage(dialog.path, passwordField.text, hintField.text, historyBox.checked)
        else if (dialog.mode === "password")
            dialog.request = Workspace.encryption.changePagePassword(dialog.path, passwordField.text, hintField.text,
                                                                     historyBox.checked)
        else
            dialog.request = Workspace.encryption.createPage(Js.trim(nameField.text), dialog.path, passwordField.text,
                                                             hintField.text)
    }

    Connections {
        target: Workspace.encryption
        function onPageConverted(id: int, path: string, error: string) {
            if (id !== dialog.request)
                return
            dialog.request = -1
            dialog.working = false
            if (path === "") {
                dialog.error = error
                dialog.failed()
                return
            }
            dialog.done(path)
            dialog.close()
        }
    }

    BText {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        text: dialog.mode === "encrypt"
              ? qsTr("The page is saved as “%1”, readable only with the password — in Brulion and in Obsidian with the Meld Encrypt plugin. Links to it follow it.").arg(Workspace.pageTitle(Workspace.encryption.encryptedPathFor(dialog.path)))
              : dialog.mode === "password"
                ? qsTr("The page is encrypted again under the new password.")
                : qsTr("A page readable only with its password, in Brulion and in Obsidian with the Meld Encrypt plugin.")
    }
    BTextField {
        id: nameField
        objectName: "encryptionDialogName"
        visible: dialog.mode === "new"
        Layout.fillWidth: true
        placeholderText: qsTr("Page name")
        leadingIcon: "file-text"
        EnterKey.type: Qt.EnterKeyNext
        onAccepted: passwordField.forceActiveFocus()
    }
    BTextField {
        id: passwordField
        objectName: "encryptionDialogPassword"
        Layout.fillWidth: true
        echoMode: TextInput.Password
        inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
        placeholderText: dialog.mode === "password" ? qsTr("New password") : qsTr("Password")
        leadingIcon: "key-round"
        EnterKey.type: Qt.EnterKeyNext
        onAccepted: confirmField.forceActiveFocus()
    }
    BTextField {
        id: confirmField
        objectName: "encryptionDialogConfirm"
        Layout.fillWidth: true
        echoMode: TextInput.Password
        inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
        placeholderText: qsTr("Repeat the password")
        leadingIcon: "key-round"
        errorText: confirmField.text !== "" && !dialog.passwordsMatch ? qsTr("The passwords differ.") : ""
        EnterKey.type: Qt.EnterKeyNext
        onAccepted: hintField.forceActiveFocus()
    }
    BTextField {
        id: hintField
        objectName: "encryptionDialogHint"
        Layout.fillWidth: true
        placeholderText: qsTr("Hint (optional, readable by anyone)")
        leadingIcon: "info"
        errorText: Workspace.encryption.validHint(hintField.text) ? "" : qsTr("A hint cannot contain 🔐, 💡 or a line break.")
        onAccepted: dialog.tryAccept()
    }
    BCheckBox {
        id: historyBox
        objectName: "encryptionDialogHistory"
        visible: dialog.mode !== "new"
        Layout.fillWidth: true
        checked: true
        text: dialog.mode === "password" ? qsTr("Delete the page's older versions") : qsTr("Delete the page's version history")
        description: dialog.mode === "password" ? qsTr("They open with the old password.")
                                                : qsTr("Its versions on this device are not encrypted.")
    }
    BText {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        variant: "muted"
        text: dialog.mode === "encrypt"
              ? qsTr("Copies made before — backups, other devices' versions, a sync server's history — stay as they were. A forgotten password cannot be recovered.")
              : qsTr("A forgotten password cannot be recovered.")
    }
    BText {
        objectName: "encryptionDialogError"
        visible: dialog.error !== ""
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        color: Theme.dangerText
        text: dialog.error
    }
}
