pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// GraphNodeCard — the highlighted node of the graph: its title, what it is (page, daily note,
// tag, page that does not exist yet) and how many links it has; with `actions` the buttons "Open"
// and "Local graph" (touch: after the first tap; desktop: for a node found by search).
//
//   GraphNodeCard { info: graphModel.nodeInfo(graph.highlightedId); actions: true
//                   onOpenRequested: …; onLocalRequested: … }
BCard {
    id: root

    property var info: ({})
    property bool actions: true

    signal openRequested()
    signal localRequested()
    signal closeRequested()

    readonly property string kindLabel: {
        switch (info.kind) {
        case "journal": return qsTr("Journal")
        case "tag": return qsTr("Tag")
        case "missing": return qsTr("Page that does not exist yet")
        }
        return qsTr("Page")
    }
    readonly property string journalDay: info.kind === "journal" ? Workspace.journalDate(info.path || "") : ""
    readonly property string displayTitle: journalDay !== "" ? DatesHelper.formatLong(journalDay) : (info.title || "")

    elevation: 3
    padding: Theme.space3
    implicitWidth: Math.min(360, Math.max(220, content.implicitWidth + 2 * padding))

    Accessible.role: Accessible.Grouping
    Accessible.name: displayTitle

    contentItem: ColumnLayout {
        id: content
        spacing: Theme.space2

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space2
            BIcon {
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: 2
                name: root.info.kind === "tag" ? "hash" : root.info.kind === "journal" ? "book-open"
                    : root.info.kind === "missing" ? "file" : "file-text"
                size: Theme.iconSize
                color: Theme.fgMuted
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Text {
                    Layout.fillWidth: true
                    text: root.displayTitle
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.textMd
                    font.weight: Theme.weightSemiBold
                    color: Theme.fg
                    wrapMode: Text.Wrap
                    maximumLineCount: 3
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    text: root.kindLabel + " · " + qsTr("%n links", "", root.info.degree || 0)
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.textSm
                    color: Theme.fgMuted
                    elide: Text.ElideRight
                }
            }
            BIconButton {
                visible: root.actions
                Layout.alignment: Qt.AlignTop
                size: "small"
                iconName: "x"
                label: qsTr("Close")
                onClicked: root.closeRequested()
            }
        }
        RowLayout {
            visible: root.actions
            Layout.fillWidth: true
            spacing: Theme.space2
            BButton {
                size: "small"
                variant: "primary"
                iconName: root.info.kind === "missing" ? "plus" : root.info.kind === "tag" ? "hash" : "file-text"
                text: root.info.kind === "missing" ? qsTr("Create") : qsTr("Open")
                onClicked: root.openRequested()
            }
            BButton {
                visible: root.info.kind === "page" || root.info.kind === "journal"
                size: "small"
                iconName: "network"
                text: qsTr("Local graph")
                onClicked: root.localRequested()
            }
        }
    }
}
