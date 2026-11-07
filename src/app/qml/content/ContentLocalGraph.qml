pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ContentLocalGraph — the side panel's "Local graph" (docs/UI.md "Side panel"): the open page and
// the pages it links to or that link to it (GraphModel with `focusPath`, depth 1, no tags), laid
// out by the graph view's own simulation in a small square that fits them all. A click on a node
// opens it; "Open graph" opens the graph view on this page (route depth 1). Made only while its
// section is open (the layout runs on a worker, and stops while the panel is hidden).
//
//   ContentLocalGraph { path: page.path }
Item {
    id: root

    property string path
    /// Pages around this one (the focus not counted).
    readonly property int count: Math.max(0, graphModel.nodeCount - 1)

    implicitHeight: column.implicitHeight

    MapHolder { id: nodeInfo }
    function openNode(id: string): void {
        nodeInfo.map = graphModel.nodeInfo(id)
        const kind = "" + nodeInfo.map.kind
        if (kind === "page" || kind === "journal")
            AppController.openPage("" + nodeInfo.map.path)
    }

    GraphModel {
        id: graphModel
        showTags: false
        showMissing: false
        showJournals: true
        focusPath: root.path
        depth: 1
        locale: DatesHelper.locale
        maxNodes: 200
        frameInterval: DeviceInfo.platform === "wasm" ? 33 : 16
        reducedMotion: Theme.reducedMotion
        active: root.visible && root.path !== "" && Application.state === Qt.ApplicationActive
        vaultIndex: root.path !== "" ? PlanningContext.vaultIndex : null
    }

    ColumnLayout {
        id: column
        width: parent.width
        spacing: Theme.space1

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(240, Math.max(160, width * 0.75))
            radius: Theme.radius
            color: Theme.bgSunken
            border.width: 1
            border.color: Theme.borderSubtle
            clip: true

            GraphItem {
                id: graph
                objectName: "localGraph"
                anchors.fill: parent
                anchors.margins: 1
                model: graphModel
                autoFit: true
                touchMode: Theme.touchUi
                animationDuration: Theme.durationSlow
                labelZoom: 0.6
                backgroundColor: Theme.bgSunken
                edgeColor: Theme.fg.alpha(Theme.dark ? 0.5 : 0.42)
                highlightColor: Theme.accent
                pageColor: Theme.link
                journalColor: Theme.markColor("journal")
                tagColor: Theme.chipRecurrenceFg
                missingColor: Theme.linkMissing
                focusColor: Theme.accent
                labelColor: Theme.fgMuted
                labelBackground: Theme.bgSunken.alpha(0.8)
                font.family: Theme.fontUi
                font.pixelSize: Theme.textXs
                Accessible.role: Accessible.Graphic
                Accessible.name: qsTr("Local graph: %n linked page(s)", "", root.count)
                onNodeActivated: (id) => root.openNode(id)
            }
            BText {
                visible: graphModel.nodeCount <= 1
                anchors.centerIn: parent
                width: parent.width - 2 * Theme.space4
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("No links to or from this page yet.")
                variant: "muted"
                font.pixelSize: Theme.textSm
                wrapMode: Text.Wrap
            }
        }
        BButton {
            Layout.leftMargin: Theme.space1
            variant: "ghost"
            size: "small"
            iconName: "waypoints"
            text: qsTr("Open graph")
            onClicked: AppController.navigate({ view: "graph", path: root.path, depth: "1" })
        }
    }
}
