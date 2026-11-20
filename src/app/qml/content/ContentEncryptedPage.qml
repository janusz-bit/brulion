pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ContentEncryptedPage — an encrypted page that is locked (PageView; docs/UI.md "Encrypted notes"):
// the page's name, its public hint and the password field. The passwords remembered in this session
// are tried first (nothing is asked when one fits). A blank page (Meld Encrypt's new note, its
// password not chosen yet) asks for a new password twice. unlocked() follows a success — the
// editor then shows the page through EncryptedPageHost.
Item {
    id: root
    objectName: "encryptedPage"

    property string path
    signal unlocked()

    property int request: -1
    property bool working: false
    property string error
    MapHolder { id: info }
    readonly property bool blank: AppController.routeFlag(info.map, "blank")
    readonly property bool readable: AppController.routeFlag(info.map, "ok")
    readonly property string hint: AppController.routeText(info.map, "hint")

    function load(): void {
        info.map = Workspace.encryption.pageInfo(root.path)
        root.error = root.readable ? "" : AppController.routeText(info.map, "error")
        passwordField.text = ""
        confirmField.text = ""
        if (root.readable && !root.blank) {
            // The passwords remembered first, without a question.
            root.working = true
            root.request = Workspace.encryption.unlockPage(root.path, "", true)
        }
    }
    onPathChanged: load()
    Component.onCompleted: load()

    function unlock(): void {
        if (root.working || passwordField.text === "" || (root.blank && passwordField.text !== confirmField.text))
            return
        root.error = ""
        root.working = true
        root.request = Workspace.encryption.unlockPage(root.path, passwordField.text, true)
        passwordField.text = ""
        confirmField.text = ""
    }

    Connections {
        target: Workspace.encryption
        function onPageUnlocked(id: int, ok: bool, error: string) {
            if (id !== root.request)
                return
            root.request = -1
            root.working = false
            if (ok) {
                root.unlocked()
                return
            }
            root.error = error
            passwordField.forceActiveFocus()
        }
    }

    Flickable {
        anchors.fill: parent
        contentHeight: Math.max(height, column.implicitHeight + 2 * Theme.space8)
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        ColumnLayout {
            id: column
            width: Math.min(parent.width - 2 * Theme.space6, 420)
            anchors.horizontalCenter: parent.horizontalCenter
            y: Math.max(Theme.space8, (parent.height - implicitHeight) / 3)
            spacing: Theme.space3

            BIcon {
                Layout.alignment: Qt.AlignHCenter
                name: "lock"
                size: 40
                color: Theme.accent
            }
            BText {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                variant: "title"
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
                text: Workspace.encryption.pageName(root.path)
            }
            BText {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                variant: "muted"
                text: !root.readable ? ""
                      : root.blank ? qsTr("This encrypted page has no password yet. Choose one to start writing.")
                      : root.working && root.request >= 0 && passwordField.text === "" ? qsTr("Unlocking…")
                      : qsTr("This page is encrypted. Enter its password to open it.")
            }
            BText {
                visible: root.hint !== ""
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                variant: "muted"
                textFormat: Text.PlainText
                text: qsTr("Hint: %1").arg(root.hint)
            }
            BTextField {
                id: passwordField
                objectName: "encryptedPagePassword"
                visible: root.readable
                Layout.fillWidth: true
                Layout.topMargin: Theme.space2
                echoMode: TextInput.Password
                inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                placeholderText: root.blank ? qsTr("New password") : qsTr("Password")
                leadingIcon: "key-round"
                enabled: !root.working
                EnterKey.type: root.blank ? Qt.EnterKeyNext : Qt.EnterKeyDone
                onAccepted: root.blank ? confirmField.forceActiveFocus() : root.unlock()
            }
            BTextField {
                id: confirmField
                objectName: "encryptedPageConfirm"
                visible: root.readable && root.blank
                Layout.fillWidth: true
                echoMode: TextInput.Password
                inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                placeholderText: qsTr("Repeat the password")
                leadingIcon: "key-round"
                errorText: confirmField.text !== "" && confirmField.text !== passwordField.text ? qsTr("The passwords differ.") : ""
                onAccepted: root.unlock()
            }
            BButton {
                objectName: "encryptedPageUnlock"
                visible: root.readable
                Layout.alignment: Qt.AlignHCenter
                variant: "primary"
                iconName: "lock-open"
                text: root.blank ? qsTr("Set password") : qsTr("Unlock")
                busy: root.working
                enabled: !root.working && passwordField.typedText !== ""
                         && (!root.blank || passwordField.text === confirmField.text)
                onClicked: root.unlock()
            }
            BText {
                objectName: "encryptedPageError"
                visible: root.error !== ""
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                color: Theme.dangerText
                text: root.error
            }
            BText {
                Layout.fillWidth: true
                Layout.topMargin: Theme.space4
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                variant: "muted"
                font.pixelSize: Theme.textSm
                text: qsTr("Encrypted with Obsidian's Meld Encrypt format. A forgotten password cannot be recovered.")
            }
        }
    }
}
