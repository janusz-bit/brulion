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
// up), switches and swatches keep their width; a wrapping group takes `evenWrapWidth()` as its
// maximum width (all on one line, or even lines).
// A row without controls titles what follows it (no divider below it: ShellSettingDividers).
//
//   ShellSettingRow { title: qsTr("Theme"); description: qsTr("…"); BSegmented { … } }
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
            if ((child as Flow) !== null || (child as BTextField) !== null)
                return false
            // (`naturalWidth`: BButton's and BSegmented's width without `fill`.)
            const button = child as BButton
            const segmented = child as BSegmented
            controls += button !== null ? button.naturalWidth : segmented !== null ? segmented.naturalWidth : child.implicitWidth
            ++shown
        }
        if (shown === 0)
            return false
        controls += (shown - 1) * slot.spacing
        const textRoom = row.width - row.columnSpacing - controls
        return titleText.implicitWidth <= textRoom && textRoom >= 0.4 * row.width
    }
    /// The row has controls (shown or not); one without titles the rows after it.
    readonly property bool hasControls: slot.children.length > 0

    /// The width for a wrapping group (the swatches' Flow: `Layout.maximumWidth`, with
    /// `Layout.fillWidth` — a fixed width wider than the row would widen the row, and the page with
    /// it) of `count` items `itemWidth` wide: one line when the row has the room, else lines holding
    /// the same number of items (4 + 4, not 7 + 1).
    function evenWrapWidth(count: int, itemWidth: real): real {
        const lines = Math.max(1, Math.ceil(count * itemWidth / Math.max(itemWidth, row.width)))
        return Math.ceil(count / lines) * itemWidth
    }

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
            for (let i = 0; i < slot.children.length; ++i)
                if (slot.children[i].visible)
                    ++n
            return n
        }
        // Stacked, a control with `fill` (BButton, BSegmented) takes the row's width: a segmented
        // control always, a button when it is the only control shown and not a ghost one. What
        // that depends on, as text: `fill` (and Layout.fillWidth) are set again when it changes.
        readonly property string fillInputs: {
            let inputs = (row.stacked ? "s" : "w") + slot.shownCount
            for (let i = 0; i < slot.children.length; ++i) {
                const button = slot.children[i] as BButton
                inputs += button !== null ? "|" + button.variant + (button.visible ? "1" : "0") : "|"
            }
            return inputs
        }
        function applyFill(): void {
            for (let i = 0; i < slot.children.length; ++i) {
                const button = slot.children[i] as BButton
                if (button !== null) {
                    button.fill = row.stacked && button.variant !== "ghost" && button.visible && slot.shownCount === 1
                    button.Layout.fillWidth = button.fill
                    continue
                }
                const segmented = slot.children[i] as BSegmented
                if (segmented !== null) {
                    segmented.fill = row.stacked
                    segmented.Layout.fillWidth = segmented.fill
                }
            }
        }
        onFillInputsChanged: slot.applyFill()
        onChildrenChanged: slot.applyFill()
        Component.onCompleted: slot.applyFill()
    }
}
