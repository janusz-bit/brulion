pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// BConfirmDialog — ask before something irreversible (delete, overwrite, discard).
//
//   BConfirmDialog {
//       id: confirmDelete
//       title: qsTr("Delete page?")
//       message: qsTr("“Trip plan” goes to the trash. You can restore it for 30 days.")
//       confirmText: qsTr("Move to trash")
//       destructive: true
//       onAccepted: vault.trash(path)
//   }
//   confirmDelete.open()
//
// `checkText` adds a check box (e.g. "Don't ask again"), read it from `checked`.
// Destructive dialogs focus the safe (cancel) button first. `message` and `detail` are plain text
// (they quote names, like the title: BDialog).
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
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            lineHeightMode: Text.FixedHeight
            lineHeight: Theme.lineHeightPx(font.pixelSize, Theme.lineHeight)
            font.family: Theme.fontUi
            font.pixelSize: Theme.textBase
            color: Theme.fg
        }
        Text {
            Layout.fillWidth: true
            visible: control.detail !== ""
            text: control.detail
            textFormat: Text.PlainText
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
