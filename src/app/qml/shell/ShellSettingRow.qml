pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ShellSettingRow — one setting: title and description on the left, the control(s) on the
// right; on compact windows the control goes below the text and its column fills the row: a
// control that can fill it (BSegmented's `fill`) takes the whole width, so every stacked row
// ends at the same right edge (buttons, switches and swatches keep their own width).
//
//   ShellSettingRow { title: qsTr("Motyw"); description: qsTr("…"); BSegmented { … } }
GridLayout {
    id: row

    property string title
    property string description
    default property alias controls: slot.data

    /// The control is under the text (compact windows).
    readonly property bool stacked: Theme.compact

    Layout.fillWidth: true
    columns: stacked ? 1 : 2
    columnSpacing: Theme.space4
    rowSpacing: Theme.space2

    ColumnLayout {
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignVCenter
        spacing: 2

        BText {
            Layout.fillWidth: true
            text: row.title
            variant: "label"
            wrapMode: Text.Wrap
        }
        BText {
            Layout.fillWidth: true
            visible: row.description !== ""
            text: row.description
            variant: "muted"
            font.pixelSize: Theme.textSm
            wrapMode: Text.Wrap
        }
    }
    RowLayout {
        id: slot
        Layout.alignment: row.stacked ? Qt.AlignLeft : (Qt.AlignRight | Qt.AlignVCenter)
        Layout.fillWidth: row.stacked
        spacing: Theme.space2
        // A child with a `fill` property (BSegmented) fills the row while stacked.
        onChildrenChanged: {
            for (let i = 0; i < children.length; ++i) {
                const child = children[i]
                if (child.fill === undefined)
                    continue
                child.fill = Qt.binding(() => row.stacked)
                child.Layout.fillWidth = Qt.binding(() => row.stacked)
            }
        }
    }
}
