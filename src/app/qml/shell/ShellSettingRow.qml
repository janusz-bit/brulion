pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ShellSettingRow — one setting: title and description on the left, the control(s) on the
// right; on compact windows the control goes below the text and its column fills the row: a
// control that can fill it (BSegmented's `fill`) takes the whole width, so every stacked row
// ends at the same right edge — a framed button too when it is the row's only shown control
// (BButton's `fill`); a button beside a badge or another button, a text button ("ghost": no frame
// to line up), switches and swatches keep their width.
// A row without controls titles what follows it (no divider below it: ShellSettingDividers).
//
//   ShellSettingRow { title: qsTr("Motyw"); description: qsTr("…"); BSegmented { … } }
GridLayout {
    id: row

    property string title
    property string description
    default property alias controls: slot.data

    /// The control is under the text (compact windows).
    readonly property bool stacked: Theme.compact
    /// The row has controls (shown or not); one without titles the rows after it.
    readonly property bool hasControls: slot.children.length > 0

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
        /// The controls shown now (a lone button fills the stacked row, one beside another does not).
        readonly property int shownCount: {
            let n = 0
            for (let i = 0; i < children.length; ++i)
                if (children[i].visible)
                    ++n
            return n
        }
        // A child with a `fill` property fills the row while stacked: BSegmented always, a
        // framed BButton only as the row's only shown control.
        onChildrenChanged: {
            for (let i = 0; i < children.length; ++i) {
                const child = children[i]
                if (child.fill === undefined)
                    continue
                const button = child instanceof BButton
                child.fill = Qt.binding(() => row.stacked && (!button || (child.variant !== "ghost" && child.visible
                                                                          && slot.shownCount === 1)))
                child.Layout.fillWidth = Qt.binding(() => child.fill)
            }
        }
    }
}
