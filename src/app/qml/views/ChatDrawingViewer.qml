pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// ChatDrawingViewer — a drawing of an AI message on its own, over the whole window, as a picture
// opens (docs/UI.md "AI chat"; a tap on it in ChatSegments): a chart laid out at the window's
// width (at most 1400 px), a Mermaid diagram fitted into the window — a small one laid out with a
// larger font (up to twice the text's), a large one scaled down; a click on it, or the size button,
// shows it at its own size, scrolled around —, a display formula large and centred. Drawn
// by the same items as in the bubble (ChartView, MermaidView, the formula's rich text): nothing of
// the message is run. The bar names what it is and holds "Copy source" (the block as written) and
// "Close"; Escape closes it too.
//
//   ChatDrawingViewer { english: … }   viewer.show(segment)   // a ChatMessageModel segment
T.Popup {
    id: viewer

    objectName: "chatDrawingViewer"

    /// The segment shown ({kind, html, code, source, chartData}).
    property var segment: ({})
    /// Numbers of a chart's axis in English.
    property bool english
    /// A diagram at its own size (scrolled) ↔ fitted into the window.
    property bool actualSize: false
    /// The diagram's font: a small diagram is laid out larger to fill the window (up to twice the
    /// text's size; MermaidView scales down only), once, when it is first drawn here.
    property real diagramFont: Theme.textBase
    property bool diagramFitted: false

    readonly property MapHolder rec: MapHolder { map: viewer.segment }
    readonly property string kind: AppController.routeText(viewer.segment, "kind")
    readonly property string code: AppController.routeText(viewer.segment, "code")
    readonly property string source: AppController.routeText(viewer.segment, "source")
    readonly property string html: AppController.routeText(viewer.segment, "html")
    readonly property ChartView chart: chartLoader.item as ChartView
    readonly property MermaidView diagram: diagramLoader.item as MermaidView
    /// The diagram fitted is smaller than its own size: the size button shows that.
    readonly property bool canZoom: viewer.diagram !== null && viewer.diagram.diagramSize.width > flick.width - 2 * viewer.margin
    readonly property real margin: Theme.compact ? Theme.space3 : Theme.space6
    readonly property string name: viewer.kind === "chart" && viewer.chart !== null && viewer.chart.typeName !== ""
                                   ? viewer.chart.typeName
                                   : viewer.kind === "diagram" ? qsTr("Diagram") : qsTr("Formula")
    readonly property real safeTop: viewer.parent ? viewer.parent.SafeArea.margins.top : 0
    readonly property real safeBottom: viewer.parent ? viewer.parent.SafeArea.margins.bottom : 0

    /// Shows `drawing` (a segment of ChatMessageModel) fitted into the window.
    function show(drawing: var): void {
        viewer.segment = drawing
        viewer.actualSize = false
        viewer.diagramFont = Theme.textBase
        viewer.diagramFitted = false
        flick.contentX = 0
        flick.contentY = 0
        viewer.open()
    }
    function toggleSize(): void {
        if (!viewer.canZoom && !viewer.actualSize)
            return
        viewer.actualSize = !viewer.actualSize
        flick.contentX = Math.max(0, (flick.contentWidth - flick.width) / 2)
        flick.contentY = 0
    }
    function copySource(): void {
        if (tools.copyText(viewer.source))
            AppController.toast(qsTr("Copied"), "success")
    }

    parent: T.Overlay.overlay
    modal: true
    dim: false
    focus: true
    closePolicy: T.Popup.CloseOnEscape
    x: 0
    y: 0
    width: parent ? parent.width : Theme.windowWidth
    height: parent ? parent.height : Theme.windowHeight
    padding: 0

    PageTools {
        id: tools
        workspace: Workspace
    }

    background: Rectangle { color: Theme.bg }

    contentItem: Item {
        // The bar: what it is and the view's controls.
        Rectangle {
            id: bar

            width: parent.width
            implicitHeight: barRow.implicitHeight + Theme.space3 * 2 + viewer.safeTop
            color: Theme.bg

            Rectangle {
                // A hairline under the bar.
                y: parent.height - 1
                width: parent.width
                height: 1
                color: Theme.borderSubtle
            }

            RowLayout {
                id: barRow

                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.bottomMargin: Theme.space3
                anchors.leftMargin: Theme.compact ? Theme.space3 : Theme.space4
                anchors.rightMargin: Theme.compact ? Theme.space2 : Theme.space4
                spacing: Theme.compact ? Theme.space1 : Theme.space3

                BIcon {
                    visible: !Theme.compact
                    name: viewer.kind === "chart" ? "chart-column" : viewer.kind === "diagram" ? "network" : "sigma"
                    size: Theme.iconSize
                    color: Theme.fgMuted
                    Layout.alignment: Qt.AlignVCenter
                }
                Text {
                    objectName: "chatDrawingViewerName"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.alignment: Qt.AlignVCenter
                    text: viewer.name
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.textSm
                    color: Theme.fgMuted
                }
                BIconButton {
                    objectName: "chatDrawingViewerSize"
                    visible: viewer.kind === "diagram" && (viewer.canZoom || viewer.actualSize)
                    size: "small"
                    iconName: viewer.actualSize ? "zoom-out" : "zoom-in"
                    label: viewer.actualSize ? qsTranslate("ContentPictureView", "Fit to view")
                                             : qsTranslate("ContentPictureView", "Zoom to actual size")
                    Layout.alignment: Qt.AlignVCenter
                    onClicked: viewer.toggleSize()
                }
                // Text where there is room, an icon on a phone.
                BButton {
                    objectName: "chatDrawingViewerCopy"
                    visible: !Theme.compact
                    size: "small"
                    variant: "ghost"
                    iconName: "copy"
                    text: qsTr("Copy source")
                    Layout.alignment: Qt.AlignVCenter
                    onClicked: viewer.copySource()
                }
                BIconButton {
                    objectName: "chatDrawingViewerCopy"
                    visible: Theme.compact
                    size: "small"
                    iconName: "copy"
                    label: qsTr("Copy source")
                    Layout.alignment: Qt.AlignVCenter
                    onClicked: viewer.copySource()
                }
                BIconButton {
                    objectName: "chatDrawingViewerClose"
                    size: "small"
                    iconName: "x"
                    label: qsTr("Close")
                    Layout.alignment: Qt.AlignVCenter
                    onClicked: viewer.close()
                }
            }
        }

        Flickable {
            id: flick

            anchors.fill: parent
            anchors.topMargin: bar.height
            anchors.bottomMargin: viewer.safeBottom
            clip: true
            contentWidth: Math.max(width, drawing.width + 2 * viewer.margin)
            contentHeight: Math.max(height, drawing.height + 2 * viewer.margin)
            boundsBehavior: Flickable.StopAtBounds
            TouchFlick.enabled: true

            Item {
                id: drawing
                readonly property real room: flick.width - 2 * viewer.margin
                x: Math.max(viewer.margin, (flick.width - width) / 2)
                y: Math.max(viewer.margin, (flick.height - height) / 2)
                width: viewer.kind === "chart" ? Math.min(drawing.room, 1400)
                       : viewer.kind === "diagram" ? (viewer.actualSize && viewer.diagram !== null
                                                      ? Math.max(drawing.room, Math.min(viewer.diagram.diagramSize.width, 4096))
                                                      : drawing.room)
                       : drawing.room
                height: viewer.kind === "chart" ? chartLoader.height
                        : viewer.kind === "diagram" ? diagramLoader.height
                        : formula.implicitHeight

                Loader {
                    id: chartLoader
                    width: parent.width
                    height: active && item !== null ? (item as Item).implicitHeight : 0
                    active: viewer.visible && viewer.kind === "chart"
                    sourceComponent: ChartView {
                        objectName: "chatDrawingViewerChart"
                        width: chartLoader.width
                        source: viewer.code
                        chartData: viewer.rec.map.chartData
                        english: viewer.english
                        fontFamily: Theme.fontUi
                        fontPixelSize: Theme.textBase
                        textColor: Theme.fg
                        mutedColor: Theme.fgMuted
                        backgroundColor: Theme.bg
                        borderColor: Theme.border
                        accentColor: Theme.accent
                        dark: Theme.dark
                        Accessible.role: Accessible.Graphic
                        Accessible.name: typeName
                        Accessible.description: description
                    }
                }
                Loader {
                    id: diagramLoader
                    width: parent.width
                    height: active && item !== null ? (item as Item).implicitHeight : 0
                    active: viewer.visible && viewer.kind === "diagram"
                    sourceComponent: MermaidView {
                        id: largeDiagram
                        objectName: "chatDrawingViewerDiagram"
                        width: diagramLoader.width
                        source: viewer.code
                        fontFamily: Theme.fontUi
                        fontPixelSize: viewer.diagramFont
                        // Drawn at the text's size first: a diagram smaller than the window is laid
                        // out again with a larger font, as a picture fitted into it is enlarged.
                        onResultChanged: {
                            const w = largeDiagram.diagramSize.width
                            const h = largeDiagram.diagramSize.height
                            if (viewer.diagramFitted || largeDiagram.status !== "ok" || w <= 0 || h <= 0)
                                return
                            viewer.diagramFitted = true
                            const k = Math.min(drawing.room / w, (flick.height - 2 * viewer.margin) / h, 2) * 0.95
                            if (k > 1.1)
                                viewer.diagramFont = Math.floor(Theme.textBase * k)
                        }
                        textColor: Theme.fg
                        mutedColor: Theme.fgMuted
                        backgroundColor: Theme.bg
                        borderColor: Theme.border
                        accentColor: Theme.accent
                        dark: Theme.dark
                        Accessible.role: Accessible.Graphic
                        Accessible.name: description

                        TapHandler { onTapped: viewer.toggleSize() }
                        HoverHandler {
                            cursorShape: viewer.canZoom || viewer.actualSize ? Qt.PointingHandCursor : Qt.ArrowCursor
                            enabled: !Theme.touchUi
                        }
                    }
                }
                BRichText {
                    id: formula
                    objectName: "chatDrawingViewerMath"
                    width: parent.width
                    visible: viewer.kind === "math"
                    html: viewer.visible && viewer.kind === "math" ? viewer.html : ""
                    horizontalAlignment: Text.AlignHCenter
                    font.family: Theme.fontEditor
                    font.pixelSize: Math.round(Theme.textXl * 1.6)
                    color: Theme.fg
                    accessibleName: viewer.code
                }
            }
        }
    }
}
