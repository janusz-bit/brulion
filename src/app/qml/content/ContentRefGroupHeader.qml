pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ContentRefGroupHeader — the page heading of a group of references (ContentBacklinks, TagsView):
// a fold arrow (GroupedRefsModel.toggleCollapsed), the page's title — journals by their date —
// that opens the page, its folder and the number of references.
//
//   ContentRefGroupHeader { refs: backlinks; path: …; title: …; folder: …; journalDate: …;
//                           count: …; collapsed: … }
Item {
    id: header
    required property var refs
    required property string path
    required property string title
    required property string folder
    required property string journalDate
    required property int count
    required property bool collapsed
    property bool compact: false

    implicitHeight: Math.max(Theme.touchUi ? Theme.touchTarget : 30, headerRow.implicitHeight)

    RowLayout {
        id: headerRow
        anchors.fill: parent
        spacing: 2
        BIconButton {
            iconName: header.collapsed ? "chevron-right" : "chevron-down"
            label: header.collapsed ? qsTr("Show references") : qsTr("Hide references")
            size: "small"
            onClicked: header.refs.toggleCollapsed(header.path)
        }
        BIcon {
            name: header.journalDate !== "" ? "calendar-days" : "file-text"
            size: Theme.iconSizeSm
            color: Theme.fgMuted
        }
        Text {
            id: titleLink
            Layout.fillWidth: true
            Layout.leftMargin: Theme.space1
            text: header.journalDate !== "" ? DatesHelper.formatLong(header.journalDate) : header.title
            textFormat: Text.PlainText
            elide: Text.ElideRight
            font.family: Theme.fontUi
            font.pixelSize: Theme.textBase
            font.weight: Theme.weightSemiBold
            font.underline: titleHover.hovered
            color: Theme.fg
            Accessible.role: Accessible.Link
            Accessible.name: text
            HoverHandler { id: titleHover; enabled: !Theme.touchUi; cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: AppController.openPage(header.path, -1, false) }
        }
        BText {
            visible: header.folder !== "" && header.journalDate === "" && !header.compact
            Layout.maximumWidth: headerRow.width * 0.35
            text: header.folder
            variant: "caption"
            color: Theme.fgFaint
            elide: Text.ElideMiddle
        }
        BBadge {
            count: header.count
            Layout.rightMargin: Theme.space1
        }
    }
}
