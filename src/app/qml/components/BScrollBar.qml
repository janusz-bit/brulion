pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// BScrollBar — thin, quiet scroll bar that widens on hover/drag.
//
//   ListView { ScrollBar.vertical: BScrollBar {} }
//
// Hidden when the content fits; fades out shortly after scrolling stops (always visible
// while hovered). On touch UIs it is only an indicator (not draggable, 4 px).
T.ScrollBar {
    id: control

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
        opacity: control.policy === T.ScrollBar.AlwaysOn || control.active || control.hovered ? 1 : 0

        Behavior on opacity { NumberAnimation { duration: Theme.duration } }
        Behavior on implicitWidth { NumberAnimation { duration: Theme.durationFast } }
        Behavior on implicitHeight { NumberAnimation { duration: Theme.durationFast } }
    }
}
