pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// BEmptyState — friendly placeholder for empty lists, no results, first run.
//
//   BEmptyState {
//       iconName: "inbox"; title: qsTr("Brak zadań na dziś")
//       description: qsTr("Dodaj zadanie w dzienniku: - [ ] …")
//       actionText: qsTr("Otwórz dziennik"); onActionTriggered: openJournal()
//   }
//
// Centre it in the empty area (anchors.centerIn / Layout.alignment). `compact` for small
// panels (smaller icon, no circle).
ColumnLayout {
    id: root

    property string iconName: "inbox"
    property string title
    property string description
    property string actionText
    property string actionIcon
    property string secondaryActionText
    property bool compact: false
    property int maximumWidth: 380

    signal actionTriggered()
    signal secondaryActionTriggered()

    spacing: compact ? Theme.space2 : Theme.space3
    width: Math.min(maximumWidth, parent ? parent.width - 2 * Theme.space6 : maximumWidth)

    Accessible.role: Accessible.Grouping
    Accessible.name: title

    Rectangle {
        Layout.alignment: Qt.AlignHCenter
        Layout.bottomMargin: root.compact ? 0 : Theme.space1
        visible: root.iconName !== ""
        implicitWidth: root.compact ? 40 : 64
        implicitHeight: implicitWidth
        radius: width / 2
        color: root.compact ? "transparent" : Theme.bgSunken
        border.width: root.compact ? 0 : 1
        border.color: Theme.borderSubtle
        BIcon {
            anchors.centerIn: parent
            name: root.iconName
            size: root.compact ? 24 : 28
            color: Theme.fgFaint
        }
    }

    Text {
        Layout.fillWidth: true
        visible: root.title !== ""
        text: root.title
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        font.family: Theme.fontUi
        font.pixelSize: root.compact ? Theme.textMd : Theme.textLg
        font.weight: Theme.weightSemiBold
        color: Theme.fg
    }

    Text {
        Layout.fillWidth: true
        visible: root.description !== ""
        text: root.description
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        lineHeightMode: Text.FixedHeight
        lineHeight: Theme.lineHeightPx(font.pixelSize)
        font.family: Theme.fontUi
        font.pixelSize: root.compact ? Theme.textSm : Theme.textBase
        color: Theme.fgMuted
    }

    Flow {
        Layout.alignment: Qt.AlignHCenter
        Layout.topMargin: Theme.space2
        Layout.maximumWidth: root.width
        visible: root.actionText !== "" || root.secondaryActionText !== ""
        spacing: Theme.space2

        BButton {
            visible: root.actionText !== ""
            text: root.actionText
            iconName: root.actionIcon
            variant: "primary"
            onClicked: root.actionTriggered()
        }
        BButton {
            visible: root.secondaryActionText !== ""
            text: root.secondaryActionText
            variant: "ghost"
            onClicked: root.secondaryActionTriggered()
        }
    }
}
