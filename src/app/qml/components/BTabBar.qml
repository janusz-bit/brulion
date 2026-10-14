pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// BTabBar — horizontal tabs with a sliding underline in the margin red.
//
//   BTabBar {
//       id: tabs
//       BTabButton { text: qsTr("Odnośniki") }
//       BTabButton { text: qsTr("Spis treści") }
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
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                height: 2
                radius: 1
                color: Theme.accent
            }
        }

        // Edge fades while tabs are scrolled out of view (`parent: tabList` keeps them on the
        // viewport instead of the scrolling content).
        readonly property bool overflowing: contentWidth > width + 1
        Rectangle {
            parent: tabList
            z: 3
            width: 28
            height: tabList.height - 1
            opacity: tabList.overflowing && !tabList.atXBeginning ? 1 : 0
            visible: opacity > 0
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: control.fadeColor }
                GradientStop { position: 1; color: Qt.rgba(control.fadeColor.r, control.fadeColor.g, control.fadeColor.b, 0) }
            }
            Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
        }
        Rectangle {
            parent: tabList
            z: 3
            x: tabList.width - width
            width: 28
            height: tabList.height - 1
            opacity: tabList.overflowing && !tabList.atXEnd ? 1 : 0
            visible: opacity > 0
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: Qt.rgba(control.fadeColor.r, control.fadeColor.g, control.fadeColor.b, 0) }
                GradientStop { position: 1; color: control.fadeColor }
            }
            Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
        }
    }

    background: Item {
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Theme.border
        }
    }
}
