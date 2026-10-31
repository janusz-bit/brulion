pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// BListItem — row for sidebars, lists and pickers: icon, title, subtitle, trailing content.
//
//   BListItem { iconName: "book-open"; text: qsTr("Dziennik"); selected: true; count: 3 }
//   BListItem { iconName: "file-text"; text: page.title; subtitle: page.folder
//               trailing: BKbd { sequence: "Ctrl+1" } }
//
// text:        the title (also the accessible name)
// titleMarkup: optional escaped rich text for the title (e.g. TextHelper.markMatches) —
//              never raw note content
// subtitle:    second line (muted, plain text: a file name is never markup); the row grows to
//              the two-line height
// count:       badge on the right (≥ 0 shows); `trailing`: any item on the right
// selected:    current item (tinted background, bold title, accent bar on the left when
//              `selectionBar`); hover/press/keyboard focus states built in
// dense:       a navigation list's row (sidebar, drawer, tree): listItemHeightDense with a
//              mouse, exactly the touch target (44 px) on touch UIs; a two-line row keeps
//              listItemHeightTwoLine
T.ItemDelegate {
    id: control

    property string iconName
    property color iconColor: selected ? Theme.fg : Theme.fgMuted
    property string titleMarkup
    property string subtitle
    property int count: -1
    property bool selected: false
    property bool selectionBar: false
    property Item trailing: null
    property bool dense: false

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(subtitle !== "" ? Theme.listItemHeightTwoLine
                             : dense ? (Theme.touchUi ? Theme.touchTarget : Theme.listItemHeightDense)
                             : Theme.listItemHeight,
                             implicitContentHeight + topPadding + bottomPadding)
    leftPadding: Theme.touchUi ? 14 : 10
    rightPadding: Theme.touchUi ? 12 : 8
    topPadding: 4
    bottomPadding: 4
    spacing: Theme.touchUi ? 14 : 10
    focusPolicy: Qt.StrongFocus
    hoverEnabled: !Theme.touchUi
    highlighted: selected

    font.family: Theme.fontUi
    font.pixelSize: Theme.textBase

    Keys.onReturnPressed: control.click()
    Keys.onEnterPressed: control.click()

    Accessible.role: Accessible.ListItem
    Accessible.name: subtitle !== "" ? text + ", " + subtitle : text
    Accessible.selected: selected

    onTrailingChanged: {
        if (trailing) {
            trailing.parent = trailingSlot
            trailing.anchors.verticalCenter = trailingSlot.verticalCenter
            trailing.anchors.right = trailingSlot.right
        }
    }

    contentItem: Item {
        implicitHeight: Math.max(textColumn.implicitHeight, icon.height)
        implicitWidth: (icon.width > 0 ? icon.width + control.spacing : 0) + Math.max(titleLabel.implicitWidth, subtitleLabel.implicitWidth)
                       + (trailingSlot.width > 0 ? trailingSlot.width + 8 : 0)

        BIcon {
            id: icon
            anchors.verticalCenter: parent.verticalCenter
            name: control.iconName
            size: Theme.iconSize
            color: control.iconColor
        }

        Column {
            id: textColumn
            anchors.verticalCenter: parent.verticalCenter
            x: icon.width > 0 ? icon.width + control.spacing : 0
            width: parent.width - x - (trailingSlot.width > 0 ? trailingSlot.width + 8 : 0)
            spacing: 1

            Text {
                id: titleLabel
                width: parent.width
                text: control.titleMarkup !== "" ? control.titleMarkup : control.text
                textFormat: control.titleMarkup !== "" ? Text.StyledText : Text.PlainText
                font.family: control.font.family
                font.pixelSize: control.font.pixelSize
                font.weight: control.selected ? Theme.weightSemiBold : Theme.weightNormal
                color: Theme.fg
                elide: Text.ElideRight
                maximumLineCount: 1
            }
            Text {
                id: subtitleLabel
                width: parent.width
                visible: control.subtitle !== ""
                text: control.subtitle
                textFormat: Text.PlainText
                font.family: Theme.fontUi
                font.pixelSize: Theme.textSm
                color: Theme.fgMuted
                elide: Text.ElideMiddle
                maximumLineCount: 1
            }
        }

        Item {
            id: trailingSlot
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            height: parent.height
            width: control.trailing && control.trailing.visible ? control.trailing.implicitWidth
                 : countBadge.visible ? countBadge.width : 0

            BBadge {
                id: countBadge
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                visible: control.count > 0 && !control.trailing
                count: control.count
                variant: control.selected ? "accent" : "neutral"
            }
        }
    }

    background: Rectangle {
        radius: Theme.radius
        color: control.selected ? Theme.bgSelected
             : control.down ? Theme.bgPressed
             : control.hovered || control.visualFocus ? Theme.bgHover
             : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.durationInstant } }

        Rectangle {
            visible: control.selected && control.selectionBar
            x: 0
            anchors.verticalCenter: parent.verticalCenter
            width: 3
            height: parent.height - 12
            radius: 1.5
            color: Theme.accent
        }

        BFocusRing {
            visible: control.visualFocus
            targetRadius: Theme.radius
            inset: true
        }
    }

    opacity: enabled ? 1 : Theme.disabledOpacity
}
