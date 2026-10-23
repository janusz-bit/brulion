pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// BKeyboardPlaceholder — a neutral grey stand-in for an on-screen keyboard: rows of blank keys
// where the real one would be. Shown for a simulated keyboard (KeyboardInsets.simulated:
// `--keyboard <px>`, the self-tests) so that screenshots show what sits on the keyboard (Main.qml,
// the editor playground). Never a real keyboard: it takes no input and has no text.
Rectangle {
    id: board

    readonly property var rows: [10, 9, 9, 5]
    readonly property real gap: Math.max(4, Math.round(width / 120))
    readonly property real rowHeight: (height - gap * (rows.length + 1)) / rows.length

    color: Theme.dark ? "#2b2f36" : "#d5d8de"
    Accessible.ignored: true

    // Taps on the stand-in do nothing (as on a real keyboard, they never reach the app).
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
    }

    Repeater {
        model: board.rows.length
        delegate: Row {
            id: keyRow
            required property int index
            readonly property int keys: board.rows[index]
            // The last row: a wide space bar in the middle.
            readonly property real keyWidth: (board.width - board.gap * (keys + 1)) / (index === board.rows.length - 1 ? 10 : keys)
            x: (board.width - width) / 2
            y: board.gap + index * (board.rowHeight + board.gap)
            spacing: board.gap
            Repeater {
                model: keyRow.keys
                delegate: Rectangle {
                    required property int index
                    readonly property bool space: keyRow.index === board.rows.length - 1 && index === 2
                    width: space ? keyRow.keyWidth * 5 + board.gap * 4 : keyRow.keyWidth
                    height: board.rowHeight
                    radius: 5
                    color: Theme.dark ? "#454a53" : "#ffffff"
                }
            }
        }
    }
}
