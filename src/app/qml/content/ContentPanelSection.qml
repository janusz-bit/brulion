pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// ContentPanelSection — one fold of the side panel's accordion (ContentSidePanel, docs/UI.md
// "Side panel"): a header row — chevron, the title in small letter-spaced capitals, a count in the
// mono font — that folds and unfolds the content below it. The content is a Component, made while
// the section is open (nothing costs anything folded: the unlinked mentions read every note, the
// local graph runs a layout). Folded or not is no setting: every panel starts afresh.
//
//   ContentPanelSection { title: qsTr("Outline"); count: 4; content: Component { … } }
ColumnLayout {
    id: section

    property string title
    /// The count after the title (-1: none).
    property int count: -1
    property bool expanded: true
    property Component content: null
    /// The content's item while open (null folded).
    readonly property Item contentItem: loader.item as Item

    spacing: 0

    T.AbstractButton {
        id: header
        objectName: section.objectName !== "" ? section.objectName + "Header" : ""
        Layout.fillWidth: true
        implicitHeight: Theme.touchUi ? Theme.touchTarget : 30
        focusPolicy: Qt.StrongFocus
        hoverEnabled: !Theme.touchUi
        leftPadding: Theme.space2
        rightPadding: Theme.space3
        Accessible.role: Accessible.Button
        Accessible.name: section.count >= 0 ? qsTr("%1, %2").arg(section.title).arg(section.count) : section.title
        Accessible.description: section.expanded ? qsTr("Expanded") : qsTr("Collapsed")
        onClicked: section.expanded = !section.expanded

        background: Rectangle {
            color: header.pressed ? Theme.bgPressed : header.hovered ? Theme.bgHover : "transparent"
            BFocusRing {
                target: header
                visible: header.visualFocus
                inset: true
            }
        }
        contentItem: RowLayout {
            spacing: Theme.space1 + 2
            BIcon {
                name: section.expanded ? "chevron-down" : "chevron-right"
                size: Theme.iconSizeSm
                color: Theme.fgFaint
            }
            Text {
                Layout.fillWidth: true
                text: section.title
                font.family: Theme.fontUi
                font.pixelSize: Theme.textXs
                font.weight: Theme.weightSemiBold
                font.capitalization: Font.AllUppercase
                font.letterSpacing: 0.8
                color: Theme.fgMuted
                elide: Text.ElideRight
                Accessible.ignored: true
            }
            Text {
                visible: section.count >= 0
                text: "" + section.count
                font.family: Theme.fontMono
                font.pixelSize: Theme.textXs
                color: Theme.fgFaint
                Accessible.ignored: true
            }
        }
    }

    Loader {
        id: loader
        Layout.fillWidth: true
        Layout.leftMargin: Theme.space1
        Layout.rightMargin: Theme.space1
        Layout.bottomMargin: section.expanded ? Theme.space3 : 0
        active: section.expanded
        visible: section.expanded
        sourceComponent: section.content
        Layout.preferredHeight: section.expanded && loader.item ? (loader.item as Item).implicitHeight : 0
    }
}
