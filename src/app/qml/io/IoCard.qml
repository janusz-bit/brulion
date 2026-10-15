pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// IoCard — a titled card of the sync center and the import/export view: icon, title, optional
// subtitle and trailing items (a badge, a button) in the header, the content below.
//
//   IoCard {
//       iconName: "cloud"; title: "Nextcloud"; subtitle: qsTr("Twój własny serwer")
//       trailing: [ BBadge { text: qsTr("Włączone"); variant: "success" } ]
//       BText { Layout.fillWidth: true; text: …; wrapMode: Text.Wrap }
//   }
//
// Children go into a ColumnLayout (use Layout.fillWidth). `tone` as in BCard ("warning",
// "danger", "success", "sunken"…); `iconColor` tints the icon.
BCard {
    id: card

    property string iconName
    property color iconColor: Theme.fgMuted
    property string title
    property string subtitle
    property alias trailing: trailingRow.data
    default property alias content: body.data
    /// Space between the header and the content items.
    property int contentSpacing: Theme.space3

    Layout.fillWidth: true
    padding: Theme.compact ? Theme.space4 : Theme.space5
    accessibleName: title

    contentItem: ColumnLayout {
        spacing: card.contentSpacing

        RowLayout {
            Layout.fillWidth: true
            visible: card.title !== "" || card.iconName !== ""
            spacing: Theme.space3

            BIcon {
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: 2
                visible: card.iconName !== ""
                name: card.iconName
                size: Theme.iconSizeLg
                color: card.iconColor
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                BText {
                    Layout.fillWidth: true
                    text: card.title
                    variant: "title"
                    wrapMode: Text.Wrap
                    Accessible.role: Accessible.Heading
                }
                BText {
                    Layout.fillWidth: true
                    visible: card.subtitle !== ""
                    text: card.subtitle
                    variant: "muted"
                    wrapMode: Text.Wrap
                }
            }
            RowLayout {
                id: trailingRow
                Layout.alignment: Qt.AlignTop
                spacing: Theme.space2
            }
        }
        ColumnLayout {
            id: body
            Layout.fillWidth: true
            spacing: card.contentSpacing
        }
    }
}
