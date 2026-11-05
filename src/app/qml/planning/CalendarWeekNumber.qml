pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// CalendarWeekNumber — the ISO week number of a calendar row (the month grid's left column, the
// week grid's corner): a button opening that week's note (docs/SYNTAX.md §1 "Weekly and monthly
// notes"; created by its first change, never by looking at it). A week with a note has its number
// in the accent colour and semi-bold, the others faint.
//
//   CalendarWeekNumber { week: 45; monday: "2026-11-02"; hasNote: true; onOpened: (monday) => … }
Item {
    id: root

    required property int week
    /// The week's Monday ("YYYY-MM-DD").
    required property string monday
    property bool hasNote: false
    /// "Tydz." above the number (the week grid's corner has room for it).
    property bool showWord: false

    signal opened(string monday)

    activeFocusOnTab: false
    Accessible.role: Accessible.Button
    Accessible.name: hasNote ? qsTr("Week %1: open its note").arg(week) : qsTr("Week %1: write its note").arg(week)
    Accessible.onPressAction: root.opened(root.monday)

    Rectangle {
        anchors.centerIn: label
        width: Math.min(root.width - 2, label.implicitWidth + 8)
        height: label.implicitHeight + 4
        radius: Theme.radius
        color: hover.hovered ? Theme.bgHover : "transparent"
    }
    Column {
        id: label
        anchors.horizontalCenter: parent.horizontalCenter
        y: root.showWord ? Math.max(0, (root.height - implicitHeight) / 2) : 6
        Text {
            visible: root.showWord
            anchors.horizontalCenter: parent.horizontalCenter
            //: Short for "week" above the ISO week number in the calendar's corner
            text: qsTr("Wk")
            font.family: Theme.fontUi
            font.pixelSize: Theme.textXs
            font.capitalization: Font.AllUppercase
            color: Theme.fgFaint
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.week > 0 ? "" + root.week : ""
            font.family: Theme.fontUi
            font.pixelSize: Theme.textXs
            font.weight: root.hasNote ? Theme.weightSemiBold : Font.Normal
            font.features: { "tnum": 1 }
            color: root.hasNote ? Theme.accentText : Theme.fgFaint
        }
    }
    HoverHandler {
        id: hover
        enabled: !Theme.touchUi
        cursorShape: Qt.PointingHandCursor
    }
    TapHandler {
        onTapped: root.opened(root.monday)
    }
    BToolTip {
        // (Not over another view once this one is hidden: a popup outlives its item's visibility.)
        visible: hover.hovered && root.visible
        text: root.hasNote ? qsTr("Open the note of week %1").arg(root.week) : qsTr("Write the note of week %1").arg(root.week)
    }
}
