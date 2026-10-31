pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// BTabButton — one tab of a BTabBar: text, optional icon and count badge.
//
//   BTabBar { BTabButton { text: qsTr("Zadania"); iconName: "list-todo"; count: 3 } }
T.TabButton {
    id: control

    property string iconName
    property int count: -1

    // Natural width (left-aligned tabs); equal widths when the bar has `stretch: true`.
    readonly property var bar: T.TabBar.tabBar
    width: bar && bar.stretch ? (bar.availableWidth - bar.spacing * (bar.count - 1)) / Math.max(1, bar.count) : implicitWidth
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             Theme.touchUi ? 48 : 38)
    leftPadding: Theme.touchUi ? 14 : 12
    rightPadding: leftPadding
    topPadding: 0
    bottomPadding: 0
    focusPolicy: Qt.TabFocus
    hoverEnabled: !Theme.touchUi
    font.family: Theme.fontUi
    font.pixelSize: Theme.textControl
    font.weight: checked ? Theme.weightSemiBold : Theme.weightMedium

    // Arrow keys move between tabs (and keep the keyboard focus on the current one).
    Keys.onLeftPressed: {
        if (bar) {
            bar.decrementCurrentIndex()
            bar.currentItem.forceActiveFocus(Qt.TabFocusReason)
        }
    }
    Keys.onRightPressed: {
        if (bar) {
            bar.incrementCurrentIndex()
            bar.currentItem.forceActiveFocus(Qt.TabFocusReason)
        }
    }

    Accessible.role: Accessible.PageTab
    Accessible.name: count > 0 ? text + " (" + count + ")" : text

    contentItem: Item {
        implicitWidth: row.implicitWidth
        implicitHeight: row.implicitHeight
        Row {
            id: row
            anchors.centerIn: parent
            spacing: 6
            BIcon {
                anchors.verticalCenter: parent.verticalCenter
                name: control.iconName
                size: Theme.iconSize
                color: control.checked ? Theme.fg : Theme.fgMuted
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: control.text
                font: control.font
                color: control.checked || control.hovered ? Theme.fg : Theme.fgMuted
                elide: Text.ElideRight
            }
            BBadge {
                // Neutral in both states: the current tab says so with its underline.
                anchors.verticalCenter: parent.verticalCenter
                count: control.count
                variant: "neutral"
            }
        }
    }

    background: Rectangle {
        color: control.down ? Theme.bgPressed : control.hovered ? Theme.bgHover : "transparent"
        radius: Theme.radiusSm
        anchors.fill: parent
        anchors.margins: 3
        anchors.bottomMargin: 5

        BFocusRing {
            visible: control.visualFocus
            targetRadius: Theme.radiusSm
            inset: true
        }
    }
}
