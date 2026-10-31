pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ShellSettingRow — one setting: title and description on the left, the control(s) on the
// right, vertically centred on the text (as the switch rows). On compact windows (< 600 px) a row
// stacks — the control below the text — only when it must: the title, a gap and the controls'
// natural widths (BSegmented's / BButton's `naturalWidth`, which `fill` never changes) are wider
// than the row, the text would keep under 40 % of it, or a control is a wrapping group (the
// swatches' Flow) or a text field (it wants the width for what is typed). Each row decides alone,
// so long translations stack by themselves. A stacked row's controls fill it: a control that can
// (BSegmented's `fill`) takes the whole width, so every stacked row ends at the same right edge as
// the inline ones — a framed button too when it is the row's only shown control (BButton's
// `fill`); a button beside a badge or another button, a text button ("ghost": no frame to line
// up), switches and swatches keep their width.
// A row without controls titles what follows it (no divider below it: ShellSettingDividers).
//
//   ShellSettingRow { title: qsTr("Motyw"); description: qsTr("…"); BSegmented { … } }
GridLayout {
    id: row

    property string title
    property string description
    default property alias controls: slot.data

    /// The control is under the text: on compact windows, when it does not fit beside it.
    readonly property bool stacked: Theme.compact && !fitsBeside
    /// Some controls are shown and they fit beside the title on one line, leaving the text at
    /// least 40 % of the row, and none of them wraps or wants the row's width.
    readonly property bool fitsBeside: {
        let controls = 0
        let shown = 0
        for (let i = 0; i < slot.children.length; ++i) {
            const child = slot.children[i]
            if (!child.visible)
                continue
            if (child instanceof Flow || child instanceof BTextField)
                return false
            controls += child.naturalWidth !== undefined ? child.naturalWidth : child.implicitWidth
            ++shown
        }
        if (shown === 0)
            return false
        controls += (shown - 1) * slot.spacing
        const textRoom = row.width - columnSpacing - controls
        return titleText.implicitWidth <= textRoom && textRoom >= 0.4 * row.width
    }
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
            id: titleText
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
