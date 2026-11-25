pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// ShellSettingDividers — 1-px hairlines between the rows of a settings card: one centred in the
// gap between each pair of consecutive shown rows of `column` (a ColumnLayout of ShellSettingRow,
// BSwitch, …), drawn over it rather than as rows of its own, so the rows (and what finds them)
// stay as they are. Bound to the rows' geometry: a row that appears or disappears (one that
// follows the theme mode) takes its line with it. Hidden and empty children (a Repeater) are no
// rows; no line under a row without controls (it titles what follows: "Sidebar" over its
// check boxes) and none above a plain text (a note on the row above it).
// The row above is the shown one nearest above by position, not by its place in `children`: a
// Repeater adds a delegate at the end of its parent's children and then moves it before itself
// (stackBefore), which emits no childrenChanged — the list kept that order (the agents' rows,
// made again when their settings change, would put lines across the rows below them).
//
//   BCard { contentHeight: rows.implicitHeight; ColumnLayout { id: rows; … }
//           ShellSettingDividers { column: rows } }
Item {
    id: root

    required property Item column
    /// The column's rows. (A list property: `column.children[i]` itself is not compiled.)
    readonly property list<Item> rows: column.children

    x: column.x
    y: column.y
    width: column.width
    height: column.height

    Repeater {
        model: root.rows
        delegate: Rectangle {
            id: line
            required property Item modelData
            required property int index
            readonly property bool isRow: modelData.visible && modelData.height > 0
            /// The index of the shown row nearest above this one (-1: this is the first).
            readonly property int aboveIndex: {
                const top = line.modelData.y
                let nearest = -1
                let nearestY = 0
                for (let i = 0; i < root.rows.length; ++i) {
                    const row = root.rows[i]
                    if (i === line.index || !row.visible || row.height <= 0 || row.y >= top)
                        continue
                    if (nearest < 0 || row.y > nearestY) {
                        nearest = i
                        nearestY = row.y
                    }
                }
                return nearest
            }
            /// That row (null: this is the first).
            readonly property Item above: aboveIndex >= 0 ? root.rows[aboveIndex] : null
            /// A row above with no controls of its own (ShellSettingRow.hasControls) takes none.
            readonly property bool aboveHasControls: {
                const row = line.above as ShellSettingRow
                return row === null || row.hasControls
            }
            visible: isRow && above !== null && (modelData as BText) === null && aboveHasControls
            width: root.width
            height: 1
            y: above ? Math.round((above.y + above.height + modelData.y) / 2) : 0
            color: Theme.borderSubtle
        }
    }
}
