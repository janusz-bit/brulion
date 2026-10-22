pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// BMenuItem — entry of a BMenu: icon, text, shortcut hint, check mark, submenu arrow. The text
// is shown as plain text: menus list page titles and folder and notebook names, which come from
// anywhere (sync, imports) and must never turn into markup.
//
//   BMenuItem { text: qsTr("Kopiuj odnośnik"); iconName: "link"; shortcut: "Ctrl+Shift+C"
//               onTriggered: copyLink() }
//   BMenuItem { text: qsTr("Pokaż ukończone"); checkable: true; checked: showDone }
//   BMenuItem { text: qsTr("Usuń"); iconName: "trash-2"; danger: true }
T.MenuItem {
    id: control

    property string iconName
    property string shortcut
    property bool danger: false

    readonly property color textColor: danger ? Theme.dangerText : Theme.fg

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             Theme.touchUi ? Theme.touchTarget : 32)
    leftPadding: 10
    rightPadding: 10
    spacing: 10
    hoverEnabled: !Theme.touchUi
    focusPolicy: Qt.NoFocus
    font.family: Theme.fontUi
    font.pixelSize: Theme.textControl

    Accessible.role: Accessible.MenuItem
    Accessible.name: text
    Accessible.description: shortcut

    contentItem: Item {
        implicitWidth: (menuIcon.width > 0 ? menuIcon.width + control.spacing : 0) + menuLabel.implicitWidth
                       + (trailing.width > 0 ? trailing.width + 24 : 0)
        implicitHeight: Math.max(menuIcon.height, menuLabel.implicitHeight)

        Row {
            id: row
            anchors.verticalCenter: parent.verticalCenter
            spacing: control.spacing
            width: parent.width - (trailing.width > 0 ? trailing.width + 16 : 0)

            // Check mark column (only when the item is checkable) or the icon.
            BIcon {
                id: menuIcon
                anchors.verticalCenter: parent.verticalCenter
                name: control.checkable ? (control.checked ? "check" : "") : control.iconName
                collapseWhenEmpty: !control.checkable
                size: Theme.iconSize
                color: control.danger ? Theme.dangerText : control.checkable ? Theme.link : Theme.fgMuted
            }
            Text {
                id: menuLabel
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(implicitWidth, Math.max(0, parent.width - x))
                text: control.text
                textFormat: Text.PlainText
                font: control.font
                color: control.textColor
                elide: Text.ElideRight
            }
        }

        Row {
            id: trailing
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            BKbd {
                anchors.verticalCenter: parent.verticalCenter
                sequence: control.shortcut
                small: true
            }
            BIcon {
                anchors.verticalCenter: parent.verticalCenter
                name: control.subMenu ? "chevron-right" : ""
                size: Theme.iconSizeSm
                color: Theme.fgMuted
            }
        }
    }

    background: Rectangle {
        implicitWidth: 200
        radius: Theme.radius
        color: control.down ? Theme.bgPressed
             : control.highlighted ? (control.danger ? Theme.dangerBg : Theme.bgHover)
             : "transparent"
    }

    opacity: enabled ? 1 : Theme.disabledOpacity
}
