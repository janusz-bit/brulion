pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// BTabBar — horizontal tabs with a sliding underline in the margin red.
//
//   BTabBar {
//       id: tabs
//       BTabButton { text: qsTr("Backlinks") }
//       BTabButton { text: qsTr("Contents") }
//   }
//   StackLayout { currentIndex: tabs.currentIndex; ... }
//
// Tabs take their natural width (left-aligned); `stretch: true` gives them equal widths
// filling the bar. Scrolls horizontally when the tabs do not fit, with the cut-off edge faded
// into `fadeColor` (set it to the surface behind the bar).
T.TabBar {
    id: control

    property bool stretch: false
    property color fadeColor: Theme.bg

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding)
    spacing: 2

    Accessible.role: Accessible.PageTabList

    contentItem: ListView {
        id: tabList
        model: control.contentModel
        currentIndex: control.currentIndex
        spacing: control.spacing
        orientation: ListView.Horizontal
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.AutoFlickIfNeeded
        snapMode: ListView.SnapToItem
        clip: true

        highlightMoveDuration: Theme.duration
        highlightResizeDuration: Theme.duration
        highlightFollowsCurrentItem: true
        highlightRangeMode: ListView.ApplyRange
        preferredHighlightBegin: 40
        preferredHighlightEnd: width - 40
        highlight: Item {
            z: 2
            Rectangle {
                x: 8
                y: parent.height - height
                width: parent.width - 16
                height: 2
                radius: 1
                color: Theme.accent
            }
        }

        // Edge fades while tabs are scrolled out of view (`parent: tabList` keeps them on the
        // viewport instead of the scrolling content).
        readonly property bool overflowing: contentWidth > width + 1
        EdgeFade {
            leading: true
            shown: tabList.overflowing && !tabList.atXBeginning
        }
        EdgeFade {
            x: tabList.width - width
            leading: false
            shown: tabList.overflowing && !tabList.atXEnd
        }
    }

    /// A fade of the bar's edge into `fadeColor`, on the viewport (not on the scrolling content).
    component EdgeFade: Rectangle {
        id: edge
        required property bool leading
        required property bool shown
        parent: tabList
        z: 3
        width: 28
        height: tabList.height - 1
        opacity: shown ? 1 : 0
        visible: opacity > 0
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: edge.leading ? control.fadeColor : control.fadeColor.alpha(0) }
            GradientStop { position: 1; color: edge.leading ? control.fadeColor.alpha(0) : control.fadeColor }
        }
        Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
    }

    background: Item {
        Rectangle {
            y: parent.height - height
            width: parent.width
            height: 1
            color: Theme.border
        }
    }
}
