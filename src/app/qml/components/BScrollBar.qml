pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// BScrollBar — thin, quiet scroll bar that widens on hover/drag.
//
//   ListView { ScrollBar.vertical: BScrollBar {} }
//
// Hidden when the content fits; fades out shortly after scrolling stops (always visible
// while hovered). On touch UIs it is only an indicator (not draggable, 4 px). A long list whose
// rows differ in height uses BRowScrollBar instead (its thumb does not jump).
// As in a web browser: the thumb dragged moves the content with the pointer; a press on the track
// beside it pages towards the pointer (7/8 of the view, smoothly like the wheel: SmoothScroll)
// and, held, goes on paging until the thumb reaches the pointer; Shift+press jumps there (Qt's).
T.ScrollBar {
    id: control

    /// Shown while this is true, too: the list moves (BRowScrollBar, which is not attached — an
    /// attached bar is activated by its Flickable).
    property bool listMoving: false
    /// The Flickable it scrolls: an attached bar's parent, BRowScrollBar's list.
    readonly property Flickable flickable: control.parent as Flickable

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)
    padding: 2
    minimumSize: 0.08
    visible: policy !== T.ScrollBar.AlwaysOff && size < 1.0
    interactive: !Theme.touchUi
    hoverEnabled: !Theme.touchUi

    background: MouseArea {
        id: track
        enabled: control.interactive && control.flickable !== null
        acceptedButtons: Qt.LeftButton
        /// -1 / +1: paging towards the content's beginning / end.
        property int direction: 0
        /// Where the pointer is along the bar.
        property real pointer: 0

        function along(x: real, y: real): real {
            return control.vertical ? y : x
        }
        /// -1 / +1: `at` (along the bar) lies before / after the thumb; 0: on it.
        function side(at: real): int {
            const thumb = control.contentItem
            const start = control.vertical ? thumb.y : thumb.x
            const length = control.vertical ? thumb.height : thumb.width
            return at < start ? -1 : (at >= start + length ? 1 : 0)
        }
        function page(): void {
            const f = control.flickable
            if (f === null)
                return
            const view = control.vertical ? f.height - f.topMargin - f.bottomMargin
                                          : f.width - f.leftMargin - f.rightMargin
            const step = track.direction * Math.max(1, view * 0.875)
            SmoothScroll.scrollBy(f, control.vertical ? 0 : step, control.vertical ? step : 0)
        }

        // On the thumb (and Shift+press): the bar's own press, which drags the thumb (or jumps).
        onPressed: (mouse) => {
            const at = track.along(mouse.x, mouse.y)
            const s = track.side(at)
            if (s === 0 || (mouse.modifiers & Qt.ShiftModifier)) {
                mouse.accepted = false
                return
            }
            track.direction = s
            track.pointer = at
            track.page()
            pager.interval = 250
            pager.restart()
        }
        onPositionChanged: (mouse) => {
            track.pointer = track.along(mouse.x, mouse.y)
        }
        onReleased: pager.stop()
        onCanceled: pager.stop()

        Timer {
            id: pager
            repeat: true
            onTriggered: {
                pager.interval = 50
                if (track.side(track.pointer) === track.direction)
                    track.page()
                else
                    pager.stop()
            }
        }
    }

    contentItem: Rectangle {
        readonly property bool wide: control.hovered || control.pressed
        implicitWidth: control.interactive ? (wide ? 8 : 5) : 4
        implicitHeight: control.interactive ? (wide ? 8 : 5) : 4
        radius: Math.min(width, height) / 2
        color: control.pressed ? Theme.fg.alpha(0.42) : Theme.scrollbar
        opacity: control.policy === T.ScrollBar.AlwaysOn || control.active || control.hovered
                 || control.listMoving ? 1 : 0

        Behavior on opacity { NumberAnimation { duration: Theme.duration } }
        Behavior on implicitWidth { NumberAnimation { duration: Theme.durationFast } }
        Behavior on implicitHeight { NumberAnimation { duration: Theme.durationFast } }
    }
}
