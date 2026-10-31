pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// BSectionHeader — small heading above a group (sidebar "Ulubione", settings sections),
// optionally collapsible, with a count and an action button.
//
//   BSectionHeader { text: qsTr("Ulubione"); count: 4; collapsible: true; expanded: favOpen
//                    onExpandedToggled: (open) => favOpen = open
//                    actionIcon: "plus"; actionLabel: qsTr("Dodaj"); onActionClicked: add() }
T.AbstractButton {
    id: control

    property int count: -1
    property bool collapsible: false
    property bool expanded: true
    property string actionIcon
    property string actionLabel

    signal actionClicked()
    /// The user expanded/collapsed the section (click, Space). Bind `expanded` to your state
    /// and update it here: the binding is kept. Without a binding the header keeps the state.
    signal expandedToggled(bool expanded)

    implicitWidth: Math.max(160, implicitContentWidth + leftPadding + rightPadding)
    // A touch target on touch UIs, collapsible or not (it is a button either way).
    implicitHeight: Math.max(Theme.touchUi ? Theme.touchTarget : 28,
                             implicitContentHeight + topPadding + bottomPadding)
    leftPadding: Theme.touchUi ? 14 : 10
    rightPadding: 4
    focusPolicy: collapsible ? Qt.StrongFocus : Qt.NoFocus
    hoverEnabled: collapsible && !Theme.touchUi
    enabled: true

    Accessible.role: collapsible ? Accessible.Button : Accessible.Heading
    Accessible.name: text
    // (Accessible has no expanded state in Qt 6.11: say it in the description.)
    Accessible.description: collapsible ? (expanded ? qsTr("expanded") : qsTr("collapsed")) : ""

    onClicked: {
        if (!collapsible)
            return
        const next = !expanded
        expandedToggled(next)
        if (expanded !== next)
            expanded = next
    }

    contentItem: Item {
        implicitHeight: Math.max(title.implicitHeight, actionButton.visible ? actionButton.implicitHeight : 0)
        implicitWidth: row.implicitWidth + (actionButton.visible ? actionButton.implicitWidth : 0)

        Row {
            id: row
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            Text {
                id: title
                anchors.verticalCenter: parent.verticalCenter
                text: control.text.toLocaleUpperCase(Qt.locale())
                font.family: Theme.fontUi
                font.pixelSize: Theme.textXs
                font.weight: Theme.weightSemiBold
                font.letterSpacing: 0.6
                color: control.hovered ? Theme.fg : Theme.fgMuted
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: control.count >= 0
                text: control.count
                font.family: Theme.fontUi
                font.pixelSize: Theme.textXs
                color: Theme.fgFaint
            }
            BIcon {
                anchors.verticalCenter: parent.verticalCenter
                name: control.collapsible ? "chevron-down" : ""
                size: 12
                color: Theme.fgFaint
                rotation: control.expanded ? 0 : -90
                Behavior on rotation { NumberAnimation { duration: Theme.durationFast } }
            }
        }

        BIconButton {
            id: actionButton
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            visible: control.actionIcon !== ""
            iconName: control.actionIcon
            label: control.actionLabel
            size: "small"
            onClicked: control.actionClicked()
        }
    }

    background: Rectangle {
        radius: Theme.radiusSm
        color: control.collapsible && control.hovered ? Theme.bgHover : "transparent"
        BFocusRing {
            visible: control.visualFocus
            targetRadius: Theme.radiusSm
            inset: true
        }
    }
}
