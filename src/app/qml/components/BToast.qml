pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// BToast — one transient notification (normally created by BToastHost.show()).
//
// kind: "info" | "success" | "warning" | "error"; optional action button ("Cofnij") and a
// close button. Inverse colours so it stands out over any content.
T.Control {
    id: control

    property string text
    property string kind: "info"
    property string actionText
    property bool closable: true

    signal actionTriggered()
    signal closeClicked()

    readonly property string iconName: kind === "success" ? "circle-check"
                                     : kind === "warning" ? "triangle-alert"
                                     : kind === "error" ? "circle-alert"
                                     : "info"
    // Status colours readable on the inverse surface (light on ink, dark on paper).
    readonly property color iconColor: {
        switch (kind) {
        case "success": return Theme.dark ? "#2a6e44" : "#7fcb98"
        case "warning": return Theme.dark ? "#835500" : "#f2b33d"
        case "error": return Theme.dark ? "#b23c31" : "#ef7a6f"
        default: return Theme.dark ? "#2c6aa3" : "#9fc3e6"
        }
    }
    readonly property color actionColor: Theme.dark ? "#285f93" : "#9fc3e6" // ≥ 4.5:1 on bgInverse

    implicitWidth: Math.min(480, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(Theme.touchUi ? 52 : 44, implicitContentHeight + topPadding + bottomPadding)
    leftPadding: 14
    rightPadding: closable ? 6 : 14
    topPadding: 7
    bottomPadding: 7
    hoverEnabled: !Theme.touchUi

    Accessible.role: Accessible.AlertMessage
    Accessible.name: text

    // Manual layout: the message wraps, and its wrapped height must drive the toast's height
    // (RowLayout would size it from the unwrapped implicit height).
    contentItem: Item {
        implicitWidth: kindIcon.width + 10 + message.implicitWidth + 8 + trailing.implicitWidth
        implicitHeight: Math.max(message.height, trailing.implicitHeight)

        BIcon {
            id: kindIcon
            anchors.verticalCenter: parent.verticalCenter
            name: control.iconName
            size: Theme.iconSize
            color: control.iconColor
        }
        Text {
            id: message
            x: kindIcon.width + 10
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(0, parent.width - x - 8 - trailing.implicitWidth)
            text: control.text
            wrapMode: Text.Wrap
            maximumLineCount: 4
            elide: Text.ElideRight
            lineHeightMode: Text.FixedHeight
            lineHeight: Theme.lineHeightPx(font.pixelSize, 1.4)
            font.family: Theme.fontUi
            font.pixelSize: Theme.textBase
            color: Theme.fgInverse
        }
        Row {
            id: trailing
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2

            T.Button {
                id: action
                anchors.verticalCenter: parent.verticalCenter
                visible: control.actionText !== ""
                text: control.actionText
                implicitHeight: Theme.touchUi ? Theme.touchTarget : 32
                implicitWidth: actionLabel.implicitWidth + 20
                focusPolicy: Qt.StrongFocus
                hoverEnabled: !Theme.touchUi
                Accessible.name: text
                onClicked: control.actionTriggered()
                contentItem: Text {
                    id: actionLabel
                    text: action.text
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.textControl
                    font.weight: Theme.weightSemiBold
                    color: control.actionColor
                }
                background: Rectangle {
                    radius: Theme.radius
                    color: action.down ? Qt.rgba(Theme.fgInverse.r, Theme.fgInverse.g, Theme.fgInverse.b, 0.16)
                         : action.hovered ? Qt.rgba(Theme.fgInverse.r, Theme.fgInverse.g, Theme.fgInverse.b, 0.10) : "transparent"
                    BFocusRing {
                        visible: action.visualFocus
                        targetRadius: Theme.radius
                    }
                }
            }
            T.Button {
                id: close
                anchors.verticalCenter: parent.verticalCenter
                visible: control.closable
                implicitWidth: Theme.touchUi ? Theme.touchTarget : 32
                implicitHeight: implicitWidth
                focusPolicy: Qt.StrongFocus
                hoverEnabled: !Theme.touchUi
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Dismiss")
                onClicked: control.closeClicked()
                contentItem: Item {
                    BIcon {
                        anchors.centerIn: parent
                        name: "x"
                        size: Theme.iconSizeSm
                        color: Qt.rgba(Theme.fgInverse.r, Theme.fgInverse.g, Theme.fgInverse.b, close.hovered ? 1 : 0.7)
                    }
                }
                background: Rectangle {
                    radius: Theme.radius
                    color: close.hovered ? Qt.rgba(Theme.fgInverse.r, Theme.fgInverse.g, Theme.fgInverse.b, 0.10) : "transparent"
                    BFocusRing {
                        visible: close.visualFocus
                        targetRadius: Theme.radius
                    }
                }
            }
        }
    }

    background: Item {
        BShadow {
            target: toastBg
            level: 2
        }
        Rectangle {
            id: toastBg
            anchors.fill: parent
            radius: Theme.radiusLg
            color: Theme.bgInverse
        }
    }
}
