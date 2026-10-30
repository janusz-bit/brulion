pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// GraphView — route "graph" ({path?}): the link graph of the vault (GraphModel over
// PlanningContext.vaultIndex, laid out by a C++ force simulation; GraphItem draws it). Pages,
// daily notes, #tags and pages that do not exist yet as coloured nodes (sized by their links),
// labels from a sufficient zoom (and for the highlighted neighbourhood).
//
// * zoom: wheel, pinch, +/−; pan: drag the background, arrows; fit: 0 / the house button;
// * drag a node to move it (the others follow); hover (mouse) or tap (touch) highlights it and
//   its neighbours; click — or a second tap — opens it (a tag opens the tag, a missing page is
//   created);
// * search ("/" or Ctrl+F) centres and selects a page; whole graph / local graph around a page
//   (route.path, or the selected node) with depth 1 or 2; toggles for tags, missing pages and
//   daily notes (remembered on this device);
// * the layout is computed on a worker thread (in slices in the browser) and unfolds over about
//   a second; it stops when it has settled and pauses while the view is hidden; with reduced
//   motion only the final layout is shown (a spinner while a fresh one is computed); phones and
//   the browser get a lower node cap, the browser a lower frame rate.
//
// Route parameters: path (local graph around that page), select (a node id to select —
// screenshots), depth (1–2).
//
// Contract with the shell (qml/shell/ShellRouteView.qml): `route`, optional `title`.
FocusScope {
    id: root

    property var route: ({})
    readonly property string title: localMode && focusTitle !== "" ? qsTr("Graph: %1").arg(focusTitle) : qsTr("Graph")

    readonly property bool lowEnd: DeviceInfo.mobile || DeviceInfo.platform === "wasm"
    property bool showTags: flag(AppSettings.value("graph/showTags", false))
    property bool showMissing: flag(AppSettings.value("graph/showMissing", false))
    property bool showJournals: flag(AppSettings.value("graph/showJournals", true))
    /// Pages of the templates folder link to everything they are used for: hidden by default.
    property bool showTemplates: flag(AppSettings.value("graph/showTemplates", false))
    property bool localMode: false
    property int depth: 1
    property string focusPath
    readonly property string focusTitle: focusPath !== "" ? Workspace.pageTitle(focusPath) : ""
    /// The card shows the selected node (search, tap) with its buttons.
    readonly property string cardId: graph.selectedId !== "" ? graph.selectedId : ""

    function flag(value) {
        return value === true || value === "true"
    }
    function applyRoute() {
        focusPath = route && route.path ? route.path : ""
        localMode = focusPath !== ""
        depth = route && (route.depth === 2 || route.depth === "2") ? 2 : 1
        graph.selectedId = ""
        graph.resetView()
        if (route && route.select)
            selectTimer.restart()
    }
    // Select once the layout had a moment to unfold (screenshots of the node card).
    Timer {
        id: selectTimer
        interval: 600
        onTriggered: if (root.route && root.route.select) root.choose(root.route.select)
    }
    function setOption(name, on) {
        switch (name) {
        case "tags": showTags = on; AppSettings.setValue("graph/showTags", on); break
        case "missing": showMissing = on; AppSettings.setValue("graph/showMissing", on); break
        case "journals": showJournals = on; AppSettings.setValue("graph/showJournals", on); break
        case "templates": showTemplates = on; AppSettings.setValue("graph/showTemplates", on); break
        }
        graph.resetView()
    }
    function setLocal(on) {
        if (on && focusPath === "" && graph.selectedId !== "") {
            const info = graphModel.nodeInfo(graph.selectedId)
            if (info.path)
                focusPath = info.path
        }
        localMode = on && focusPath !== ""
        graph.resetView()
    }
    function showLocal(id) {
        const info = graphModel.nodeInfo(id)
        if (!info.path)
            return
        focusPath = info.path
        localMode = true
        graph.selectedId = ""
        graph.resetView()
    }
    function openNode(id) {
        const info = graphModel.nodeInfo(id)
        switch (info.kind) {
        case "tag": AppController.openTag(info.title.replace(/^#/, "")); break
        case "missing": {
            // The id is the link target as written ("missing:" + name when a note has that path).
            const name = info.id.startsWith("missing:") ? info.id.slice(8) : info.id
            AppController.openPage(Workspace.newPagePath(name))
            break
        }
        case "page":
        case "journal": AppController.openPage(info.path); break
        }
    }
    function choose(id) {
        graph.selectedId = id
        graph.centerOn(id, Math.max(graph.zoom, graph.labelZoom * 1.2))
        graph.forceActiveFocus()
    }

    onRouteChanged: applyRoute()
    Component.onCompleted: {
        applyRoute()
        graph.forceActiveFocus()
    }
    focus: true

    GraphModel {
        id: graphModel
        // The settings before the index (Qt applies the bindings in this order): opening the view
        // builds the graph once, with them — not once per setting that differs from its default.
        showTags: root.showTags
        showMissing: root.showMissing
        showJournals: root.showJournals
        showTemplates: root.showTemplates
        focusPath: root.localMode ? root.focusPath : ""
        depth: root.depth
        locale: DatesHelper.locale
        maxNodes: root.lowEnd ? 1200 : 4000
        // Phones get 60 frames a second (the layout runs on a worker); the browser computes it on
        // the GUI thread in slices between its frames.
        frameInterval: DeviceInfo.platform === "wasm" ? 33 : 16
        reducedMotion: Theme.reducedMotion
        active: root.visible && Qt.application.state === Qt.ApplicationActive
        vaultIndex: PlanningContext.vaultIndex
    }

    GraphItem {
        id: graph
        objectName: "graphItem"
        anchors.fill: parent
        model: graphModel
        focus: true
        activeFocusOnTab: true
        touchMode: Theme.touchUi
        animationDuration: Theme.durationSlow
        labelZoom: Theme.touchUi ? 1.0 : 1.15
        // Keep the nodes clear of the toolbar (top) and the counts / zoom buttons (bottom).
        fitTopMargin: toolbar.visible ? toolbar.y + toolbar.implicitHeight + Theme.space2 : Theme.space8
        fitBottomMargin: Theme.narrow ? Theme.space12 : Theme.space10
        backgroundColor: Theme.bg
        edgeColor: Theme.alpha(Theme.fg, Theme.dark ? 0.2 : 0.17)
        highlightColor: Theme.accent
        pageColor: Theme.link
        journalColor: Theme.markColor("journal")
        tagColor: Theme.chipRecurrenceFg
        missingColor: Theme.linkMissing
        focusColor: Theme.warning
        labelColor: Theme.fg
        labelBackground: Theme.alpha(Theme.bg, 0.8)
        font.family: Theme.fontUi
        font.pixelSize: Theme.textXs

        Accessible.role: Accessible.Graphic
        Accessible.name: qsTr("Link graph: %n pages", "", graphModel.nodeCount)
        Accessible.description: qsTr("Use the search field to find a page.")

        onNodeActivated: (id) => root.openNode(id)
        onBackgroundTapped: selectedId = ""

        Keys.onPressed: (event) => {
            const step = 60
            switch (event.key) {
            case Qt.Key_Plus:
            case Qt.Key_Equal: graph.zoomBy(1.25); break
            case Qt.Key_Minus: graph.zoomBy(0.8); break
            case Qt.Key_0: graph.fitAll(); break
            case Qt.Key_Left: graph.panBy(step, 0); break
            case Qt.Key_Right: graph.panBy(-step, 0); break
            case Qt.Key_Up: graph.panBy(0, step); break
            case Qt.Key_Down: graph.panBy(0, -step); break
            case Qt.Key_Return:
            case Qt.Key_Enter:
                if (graph.selectedId !== "")
                    root.openNode(graph.selectedId)
                break
            case Qt.Key_Escape:
                if (graph.selectedId === "")
                    return
                graph.selectedId = ""
                break
            case Qt.Key_Slash:
                toolbar.focusSearch()
                break
            case Qt.Key_F:
                if (!(event.modifiers & Qt.ControlModifier))
                    return
                toolbar.focusSearch()
                break
            default:
                return
            }
            event.accepted = true
        }
    }

    // ---- Overlays ------------------------------------------------------------------------------

    GraphToolbar {
        id: toolbar
        x: Theme.narrow ? Theme.space2 : Theme.space4
        y: Theme.narrow ? Theme.space2 : Theme.space4
        width: Math.min(implicitWidth, root.width - 2 * x)
        visible: graphModel.pageCount > 0
        model: graphModel
        localMode: root.localMode
        depth: root.depth
        localAvailable: root.focusPath !== "" || graph.selectedId !== ""
        showTags: root.showTags
        showMissing: root.showMissing
        showJournals: root.showJournals
        showTemplates: root.showTemplates
        onNodeChosen: (id) => root.choose(id)
        onLocalModeRequested: (on) => root.setLocal(on)
        onDepthRequested: (d) => {
            root.depth = d
            graph.resetView()
        }
        onOptionToggled: (name, on) => root.setOption(name, on)
    }

    // Hover tooltip (mouse): title and links of the node under the pointer.
    GraphNodeCard {
        visible: !Theme.touchUi && graph.hoveredId !== "" && graph.hoveredId !== graph.selectedId
        actions: false
        info: graph.hoveredId !== "" ? graphModel.nodeInfo(graph.hoveredId) : ({})
        x: Math.max(Theme.space2, Math.min(root.width - width - Theme.space2, graph.highlightedPosition.x + 16))
        y: Math.max(Theme.space2, Math.min(root.height - height - Theme.space2, graph.highlightedPosition.y + 16))
        z: 5
    }

    // The selected node (search, tap): docked at the bottom on phones, next to the node elsewhere.
    GraphNodeCard {
        id: card
        visible: root.cardId !== "" && graphModel.indexOf(root.cardId) >= 0
        actions: true
        info: root.cardId !== "" ? graphModel.nodeInfo(root.cardId) : ({})
        // Phones: left of the zoom controls, level with their bottom (never over them).
        width: Theme.compact ? root.width - 2 * Theme.space3 - (zoomControls.visible ? zoomControls.width + Theme.space2 : 0)
                             : implicitWidth
        x: Theme.compact ? Theme.space3
                         : Math.max(Theme.space2, Math.min(root.width - width - Theme.space2, graph.highlightedPosition.x + 18))
        y: Theme.compact ? Math.max(Theme.space2, (zoomControls.visible ? zoomControls.y + zoomControls.height
                                                                        : root.height - Theme.space16 - Theme.space8) - height)
                         : Math.max(Theme.space2, Math.min(root.height - height - Theme.space2, graph.highlightedPosition.y + 18))
        z: 6
        onOpenRequested: root.openNode(root.cardId)
        onLocalRequested: root.showLocal(root.cardId)
        onCloseRequested: graph.selectedId = ""
    }

    // Zoom controls.
    BCard {
        id: zoomControls
        visible: graphModel.nodeCount > 0
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: Theme.narrow ? Theme.space2 : Theme.space4
        // Phones: above the shell's floating capture button.
        anchors.bottomMargin: Theme.narrow ? Theme.space16 + Theme.space8 : Theme.space4
        elevation: 2
        padding: 2
        contentItem: Column {
            spacing: 0
            BIconButton {
                iconName: "plus"
                label: qsTr("Zoom in")
                shortcut: "+"
                onClicked: graph.zoomBy(1.3)
            }
            BIconButton {
                iconName: "minus"
                label: qsTr("Zoom out")
                shortcut: "-"
                onClicked: graph.zoomBy(1 / 1.3)
            }
            BIconButton {
                iconName: "house"
                label: qsTr("Show everything")
                shortcut: "0"
                onClicked: graph.fitAll()
            }
            BIconButton {
                iconName: "refresh-cw"
                label: qsTr("Arrange again")
                onClicked: {
                    graphModel.relayout()
                    graph.resetView()
                }
            }
        }
    }

    // Counts (and the cap).
    Text {
        visible: graphModel.nodeCount > 0
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.narrow ? Theme.space3 : Theme.space4
        anchors.bottomMargin: Theme.narrow ? Theme.space3 : Theme.space4
        width: Math.min(implicitWidth, root.width - zoomControls.width - 3 * Theme.space4)
        text: {
            const parts = [qsTr("%n nodes", "", graphModel.nodeCount), qsTr("%n links", "", graphModel.edgeCount)]
            if (graphModel.truncated)
                parts.push(qsTr("the %n best connected shown", "", graphModel.nodeCount))
            return parts.join(" · ")
        }
        font.family: Theme.fontUi
        font.pixelSize: Theme.textXs
        color: Theme.fgFaint
        elide: Text.ElideRight
    }

    // Legend (wide windows).
    Row {
        visible: !Theme.compact && graphModel.nodeCount > 0
        anchors.right: zoomControls.left
        anchors.rightMargin: Theme.space3
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.space4 + 6
        spacing: Theme.space3
        Repeater {
            model: 4
            delegate: Row {
                id: legendItem
                required property int index
                readonly property color color: [graph.pageColor, graph.journalColor, graph.tagColor, graph.missingColor][index]
                visible: [true, root.showJournals, root.showTags, root.showMissing][index]
                spacing: 4
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 8
                    height: 8
                    radius: 4
                    color: legendItem.index === 3 ? "transparent" : legendItem.color
                    border.width: legendItem.index === 3 ? 1.5 : 0
                    border.color: legendItem.color
                }
                Text {
                    text: [qsTr("Page"), qsTr("Journal"), qsTr("Tag"), qsTr("Missing page")][legendItem.index]
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.textXs
                    color: Theme.fgMuted
                }
            }
        }
    }

    // ---- Empty states --------------------------------------------------------------------------
    BEmptyState {
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * Theme.space6, 420)
        visible: graphModel.pageCount === 0 && !Workspace.indexing
        iconName: "waypoints"
        title: qsTr("The graph is empty")
        description: qsTr("Pages and the [[links]] between them appear here. Link two pages to see the first connection.")
    }
    BEmptyState {
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * Theme.space6, 420)
        visible: graphModel.pageCount > 0 && graphModel.nodeCount === 0
        iconName: "filter"
        title: qsTr("Nothing to show")
        description: qsTr("All pages are hidden by the options.")
        actionText: qsTr("Show daily notes")
        onActionTriggered: root.setOption("journals", true)
    }
    BEmptyState {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Theme.space16 * 2
        width: Math.min(parent.width - 2 * Theme.space6, 420)
        visible: root.localMode && graphModel.focusMissing
        compact: true
        iconName: "file"
        title: qsTr("“%1” is not in the graph").arg(root.focusTitle)
        description: qsTr("The page was deleted or is hidden by the options. Showing the whole graph.")
    }
    BSpinner {
        anchors.centerIn: parent
        size: Theme.iconSizeLg
        visible: graphModel.pageCount === 0 && Workspace.indexing
        running: visible
    }
    // Reduced motion: a fresh layout is computed without animation; this instead of a spiral that jumps.
    BSpinner {
        objectName: "graphSettlingSpinner"
        anchors.centerIn: parent
        size: Theme.iconSizeLg
        visible: graphModel.settling && graphModel.nodeCount > 0
        running: visible
    }
}
