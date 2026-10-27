pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// BRowScrollBar — the scroll bar of a long list whose rows differ a lot in height (the block
// editor, the journal stream, Tasks, the logs, the history): its thumb moves only with the content.
//
//   ListView { id: list; BRowScrollBar { list: list } }
//
// Qt's attached bar (BScrollBar) takes its place from ListView's estimate of the content height,
// which changes whenever rows are created or destroyed while the list scrolls: its thumb jumps.
// This one counts rows at the view's edges (RowScroll, src/app/ui/rowscroll.h; docs/UI.md "The
// scroll bar: RowScroll"); it is not attached (an attached bar's position and size are overwritten
// from visibleArea), so it places itself over the list's right edge, between its margins, and
// shows while the list moves. A wheel over it goes on to the list (a ScrollBar takes no wheel
// events); dragging it moves the list a frame at a time. Touch UIs: an indicator, as BScrollBar.
BScrollBar {
    id: bar

    required property ListView list

    objectName: "rowScrollBar"
    orientation: Qt.Vertical
    x: mirrored ? 0 : list.width - width
    y: list.topMargin
    height: Math.max(0, list.height - list.topMargin - list.bottomMargin)
    z: 1
    size: rows.size
    listMoving: list.moving

    RowScroll {
        id: rows
        view: bar.list
        dragging: bar.pressed
    }

    // The bar's position follows the rows unless it is held: then it moves the list (and is not
    // pushed back while the list follows it).
    onPositionChanged: if (pressed) rows.scrollTo(position)
    onPressedChanged: if (!pressed) position = rows.position
    Connections {
        target: rows
        function onPositionChanged() {
            if (!bar.pressed)
                bar.position = rows.position
        }
    }

    // Declared inside the ListView, the bar landed in its content item (which scrolls): over the
    // view instead, like an attached bar.
    Component.onCompleted: {
        if (parent !== list)
            parent = list
        position = rows.position
    }
}
