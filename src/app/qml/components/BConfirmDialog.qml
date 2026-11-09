pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// BConfirmDialog — ask before something irreversible (delete, overwrite, discard).
//
//   BConfirmDialog {
//       id: confirmDelete
//       title: qsTr("Move to trash?")
//       subject: Workspace.pageTitle(path)
//       message: qsTr("It will be moved to the trash. You can restore it from there.")
//       confirmText: qsTr("Move to trash")
//       destructive: true
//       onAccepted: vault.trash(path)
//   }
//   confirmDelete.open()
//
// `subject` is what the question is about — a page's title, a folder's or a file's name — shown
// as written in a frame of its own above the message, never inside a sentence: a name can be
// anything (sync, imports), and on its own line it cannot pass for the app's words.
// `checkText` adds a check box (e.g. "Don't ask again"), read it from `checked`.
// Destructive dialogs focus the safe (cancel) button first. `subject`, `message` and `detail` are
// plain text (like the title: BDialog).
BDialog {
    id: control

    /// The item asked about, shown in a frame ("" = none).
    property string subject
    /// Its icon: "file-text" (a page), "folder", "calendar-days" (a day of the journal), "file"…
    property string subjectIcon: "file-text"
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
        Rectangle {
            objectName: "confirmSubject"
            Layout.fillWidth: true
            Layout.bottomMargin: Theme.space1
            visible: control.subject !== ""
            implicitHeight: subjectRow.implicitHeight + 2 * Theme.space2
            radius: Theme.radius
            color: Theme.bgSunken
            border.width: 1
            border.color: Theme.border

            RowLayout {
                id: subjectRow
                anchors.fill: parent
                anchors.margins: Theme.space2
                anchors.leftMargin: Theme.space3
                anchors.rightMargin: Theme.space3
                spacing: Theme.space2
                BIcon {
                    // On the first line's middle, also when a long name wraps.
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: Math.max(0, Math.round((subjectMetrics.height - Theme.iconSize) / 2))
                    name: control.subjectIcon
                    size: Theme.iconSize
                    color: Theme.fgMuted
                }
                Text {
                    id: subjectText
                    objectName: "confirmSubjectText"
                    Layout.fillWidth: true
                    text: control.subject
                    textFormat: Text.PlainText
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    maximumLineCount: 3
                    elide: Text.ElideRight
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.textBase
                    font.weight: Theme.weightMedium
                    color: Theme.fg
                }
                FontMetrics {
                    id: subjectMetrics
                    font: subjectText.font
                }
            }
        }
        BText {
            Layout.fillWidth: true
            visible: control.message !== ""
            text: control.message
            wrapMode: Text.Wrap
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
