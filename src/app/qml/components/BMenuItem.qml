pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// BMenuItem — entry of a BMenu: icon, text, shortcut hint, check mark, submenu arrow. The text
// is shown as plain text: menus list page titles and folder and notebook names, which come from
// anywhere (sync, imports) and must never turn into markup.
//
//   BMenuItem { text: qsTr("Copy link"); iconName: "link"; shortcut: "Ctrl+Shift+C"
//               onTriggered: copyLink() }
//   BMenuItem { text: qsTr("Show completed"); checkable: true; checked: showDone }
//   BMenuItem { text: qsTr("Delete"); iconName: "trash-2"; danger: true }
//   BMenuItem { text: qsTr("Move up"); unavailable: gaps ? qsTr("…why…") : "" }
//   BMenuItem { text: qsTr("Tomorrow"); hint: "Wed, Nov 11" }   // muted text on the right
// `unavailable`: shown but not possible now — drawn disabled, the reason in a tooltip on hover;
// it still triggers (and closes the menu), so the menu can say why with a toast on touch screens,
// where there is no hover.
// Keyboard (BMenu): Up / Down reach every item shown, Enter, Return and Space trigger it (Qt's
// MenuItem), so a menu needs no key handlers of its own.
// Destructive items, one rule for every menu (docs/UI.md "Design system in code"): an item that
// deletes is the last of its menu or section, with a separator above it (and below it when
// sections follow); `danger` only for deleting content or files (the trash, a block, a row or
// column, a reminder, a backdrop, a history or a log), never for the "none" value of a choice
// (No status, No priority, Don't repeat: a choice like the others); a clear that cannot be undone
// asks first (its text ends with "…", a BConfirmDialog).
T.MenuItem {
    id: control

    property string iconName
    /// A colour square in the icon's place (the highlight colours); transparent: none.
    property color swatch: "transparent"
    property string shortcut
    /// A muted plain text on the right (what the item stands for: a date), "" = none.
    property string hint
    property bool danger: false
    property string unavailable: ""

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
    // T.Menu's Up / Down move only to items that take the focus from Tab, and it does not look at
    // visibility: an item hidden with `visible: false` (one only some rows have) takes none. A click
    // takes no focus (TabFocus only). The focus stays inside the menu's popup, which took it on
    // opening: what had it before (an edited text, its selection kept) gets it back on closing.
    focusPolicy: visible ? Qt.TabFocus : Qt.NoFocus
    font.family: Theme.fontUi
    font.pixelSize: Theme.textControl

    Accessible.role: Accessible.MenuItem
    Accessible.name: text
    Accessible.description: unavailable !== "" ? unavailable : hint !== "" ? hint : shortcut

    // A press inside a menu: it no longer moves out of the finger's way (BMenu.touched).
    onPressedChanged: {
        const owner = control.menu as BMenu
        if (pressed && owner)
            owner.touched = true
    }

    contentItem: Item {
        implicitWidth: (menuIcon.width > 0 ? menuIcon.width + control.spacing : 0) + menuLabel.implicitWidth
                       + (trailing.width > 0 ? trailing.width + 24 : 0)
        implicitHeight: Math.max(menuIcon.height, menuLabel.implicitHeight)

        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: control.spacing
            width: parent.width - (trailing.width > 0 ? trailing.width + 16 : 0)

            // Check mark column (only when the item is checkable) or the icon.
            BIcon {
                id: menuIcon
                anchors.verticalCenter: parent.verticalCenter
                // A submenu's item: the submenu's icon (BMenu.iconName).
                name: control.swatch.a > 0 ? ""
                    : control.checkable ? (control.checked ? "check" : "")
                    : control.iconName !== "" ? control.iconName
                    : (control.subMenu as BMenu)?.iconName ?? ""
                collapseWhenEmpty: !control.checkable && control.swatch.a === 0
                size: Theme.iconSize
                color: control.danger ? Theme.dangerText : control.checkable ? Theme.link : Theme.fgMuted
                Rectangle {
                    visible: control.swatch.a > 0
                    anchors.centerIn: parent
                    width: Math.round(parent.width * 0.8)
                    height: width
                    radius: Theme.radiusXs
                    color: control.swatch
                    border.width: 1
                    border.color: Theme.border
                }
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
            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: control.hint !== ""
                text: control.hint
                textFormat: Text.PlainText
                font.family: Theme.fontUi
                font.pixelSize: Theme.textSm
                color: Theme.fgMuted
            }
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

    opacity: enabled && unavailable === "" ? 1 : Theme.disabledOpacity

    Loader {
        active: control.unavailable !== "" && control.hovered && !Theme.touchUi
        sourceComponent: BToolTip {
            parent: control
            text: control.unavailable
            visible: true
        }
    }
}
