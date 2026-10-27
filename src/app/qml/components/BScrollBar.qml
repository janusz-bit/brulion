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
T.ScrollBar {
    id: control

    /// Shown while this is true, too: the list moves (BRowScrollBar, which is not attached — an
    /// attached bar is activated by its Flickable).
    property bool listMoving: false

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)
    padding: 2
    minimumSize: 0.08
    visible: policy !== T.ScrollBar.AlwaysOff && size < 1.0
    interactive: !Theme.touchUi
    hoverEnabled: !Theme.touchUi

    contentItem: Rectangle {
        readonly property bool wide: control.hovered || control.pressed
        implicitWidth: control.interactive ? (wide ? 8 : 5) : 4
        implicitHeight: control.interactive ? (wide ? 8 : 5) : 4
        radius: Math.min(width, height) / 2
        color: control.pressed ? Qt.rgba(Theme.fg.r, Theme.fg.g, Theme.fg.b, 0.42) : Theme.scrollbar
        opacity: control.policy === T.ScrollBar.AlwaysOn || control.active || control.hovered
                 || control.listMoving ? 1 : 0

        Behavior on opacity { NumberAnimation { duration: Theme.duration } }
        Behavior on implicitWidth { NumberAnimation { duration: Theme.durationFast } }
        Behavior on implicitHeight { NumberAnimation { duration: Theme.durationFast } }
    }
}
