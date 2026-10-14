pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// BConfirmDialog — ask before something irreversible (delete, overwrite, discard).
//
//   BConfirmDialog {
//       id: confirmDelete
//       title: qsTr("Usunąć stronę?")
//       message: qsTr("„Plan podróży” trafi do kosza. Możesz ją przywrócić przez 30 dni.")
//       confirmText: qsTr("Przenieś do kosza")
//       destructive: true
//       onAccepted: vault.trash(path)
//   }
//   confirmDelete.open()
//
// `checkText` adds a check box (e.g. "Nie pytaj ponownie"), read it from `checked`.
// Destructive dialogs focus the safe (cancel) button first.
BDialog {
    id: control

    property string message
    property string detail
    property string confirmText: qsTr("Confirm")
    property bool destructive: false
    property string checkText
    property alias checked: check.checked

    preferredWidth: Theme.dialogWidthSm
    acceptText: confirmText
    acceptVariant: destructive ? "danger" : "primary"
    iconName: destructive ? "triangle-alert" : ""
    iconColor: destructive ? Theme.dangerText : Theme.fgMuted
    focusReject: destructive

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 6
        Text {
            Layout.fillWidth: true
            visible: control.message !== ""
            text: control.message
            wrapMode: Text.Wrap
            lineHeightMode: Text.FixedHeight
            lineHeight: Theme.lineHeightPx(font.pixelSize)
            font.family: Theme.fontUi
            font.pixelSize: Theme.textBase
            color: Theme.fg
        }
        Text {
            Layout.fillWidth: true
            visible: control.detail !== ""
            text: control.detail
            wrapMode: Text.Wrap
            font.family: Theme.fontUi
            font.pixelSize: Theme.textSm
            color: Theme.fgMuted
        }
    }
    BCheckBox {
        id: check
        Layout.fillWidth: true
        visible: control.checkText !== ""
        text: control.checkText
    }
}
