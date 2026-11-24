pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ExportPasswordBox — "Protect with a password" of the HTML document export (ExportPanel and
// ContentExportDialog; shown where ExportController.protectApply): the switch, the password
// typed twice and what it means. The file becomes the lock page of password-protected
// publishing (publish/protect.h): encrypted, the browser asks for the password. The password
// lives only in the controller (setPassword(), given only while both fields match); reset()
// forgets it.
//
//   ExportPasswordBox { id: passwordBox; exporter: exportController }
//   BButton { enabled: passwordBox.ready }
ColumnLayout {
    id: box

    required property ExportController exporter
    /// Nothing to protect, or the password typed twice the same.
    readonly property bool ready: !box.exporter.protectApply || !box.exporter.protect || box.exporter.hasPassword

    function reset(): void {
        passwordField.text = ""
        confirmField.text = ""
        protectSwitch.checked = false
        box.exporter.protect = false
        box.exporter.setPassword("")
    }
    function updatePassword(): void {
        box.exporter.setPassword(passwordField.text === confirmField.text ? passwordField.text : "")
    }

    visible: box.exporter.protectApply
    spacing: Theme.space1

    BSwitch {
        id: protectSwitch
        objectName: "exportProtect"
        Layout.fillWidth: true
        text: qsTr("Protect with a password")
        iconName: "lock"
        description: qsTr("The file is encrypted; the browser asks for the password and opens it.")
        enabled: !box.exporter.busy
        onToggled: box.exporter.protect = protectSwitch.checked
    }
    BTextField {
        id: passwordField
        objectName: "exportPassword"
        Layout.fillWidth: true
        visible: protectSwitch.checked
        enabled: !box.exporter.busy
        echoMode: TextInput.Password
        inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase | Qt.ImhHiddenText
        placeholderText: qsTr("Password")
        leadingIcon: "key-round"
        EnterKey.type: Qt.EnterKeyNext
        onTextChanged: box.updatePassword()
        onAccepted: confirmField.forceActiveFocus()
    }
    BTextField {
        id: confirmField
        objectName: "exportPasswordConfirm"
        Layout.fillWidth: true
        visible: protectSwitch.checked
        enabled: !box.exporter.busy
        echoMode: TextInput.Password
        inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase | Qt.ImhHiddenText
        placeholderText: qsTr("Repeat the password")
        leadingIcon: "key-round"
        errorText: confirmField.text !== "" && confirmField.text !== passwordField.text ? qsTr("The passwords differ.") : ""
        onTextChanged: box.updatePassword()
    }
    BText {
        Layout.fillWidth: true
        visible: protectSwitch.checked
        variant: "small"
        color: Theme.fgMuted
        wrapMode: Text.Wrap
        text: qsTr("A forgotten password cannot be recovered. Anyone with the file may try passwords: choose a long one, several words. The file opens in a current browser, also from a disk or an e-mail.")
    }
}
