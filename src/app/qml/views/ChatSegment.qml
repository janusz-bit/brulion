pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// ChatSegment — one part of an AI message with a drawing (ChatSegments; docs/UI.md "AI chat"):
// its text as the bubble's rich text, or its drawing — a ```chart by ChartView (the numbers of the
// message's own table: ChatMessageModel's chartData), a ```mermaid diagram by MermaidView (scaled
// down to the bubble, never up), a display formula as rich text, centred and larger — as the
// editor draws them: Brulion's own renderers, nothing of the message is run. A diagram that cannot
// be drawn after all (MermaidView's status) shows its block as written, as a message without
// drawings does. Takes no pointer input: ChatSegments asks linkAt() and whether a drawing is shown.
Item {
    id: part

    /// ChatMessageModel's segment: {kind: "text" | "chart" | "diagram" | "math", html, code,
    /// source, chartData}.
    required property var modelData
    required property int index
    property string hoveredLink
    /// Numbers of a chart's axis in English (`12.5`).
    property bool english

    readonly property string kind: AppController.routeText(part.modelData, "kind")
    readonly property string html: AppController.routeText(part.modelData, "html")
    readonly property string code: AppController.routeText(part.modelData, "code")
    /// The block as written (Copy source).
    readonly property string source: AppController.routeText(part.modelData, "source")
    readonly property MapHolder rec: MapHolder { map: part.modelData }
    readonly property ChartView chart: chartLoader.item as ChartView
    readonly property MermaidView diagram: diagramLoader.item as MermaidView
    /// The drawing is shown (else the block as written: a text part, or a diagram Brulion cannot draw).
    readonly property bool drawn: part.kind === "math"
                                  || (part.kind === "chart" && part.chart !== null && part.chart.status === "ok")
                                  || (part.kind === "diagram" && part.diagram !== null && part.diagram.status === "ok")
    /// The part's own width (the bubble is as wide as its widest part): a chart takes all the
    /// room it is given, a diagram its natural width.
    final property real naturalWidth: part.kind === "chart" && part.drawn ? 100000
                                      : part.kind === "diagram" && part.drawn ? part.diagram.diagramSize.width
                                      : part.kind === "math" ? formula.implicitWidth
                                      : text.implicitWidth
    /// What a screen reader says of a drawing ("" for text).
    readonly property string description: part.kind === "chart" && part.chart !== null
                                          ? part.chart.typeName + ": " + part.chart.description
                                          : part.kind === "diagram" && part.diagram !== null ? part.diagram.description
                                          : ""

    implicitHeight: part.kind === "math" ? formula.implicitHeight
                    : part.kind === "chart" && part.drawn ? chartLoader.height
                    : part.kind === "diagram" && part.drawn ? diagramLoader.height
                    : text.implicitHeight
    height: implicitHeight

    /// The link at (x, y) of this part ("" none, also on a drawing).
    function linkAt(x: real, y: real): string {
        return text.visible ? text.linkAt(x, y) : ""
    }

    // The text, or the block as written (a diagram that cannot be drawn).
    BRichText {
        id: text
        measured: true // (the bubble is as wide as its text)
        width: parent.width
        visible: part.kind !== "math" && !part.drawn
        html: part.kind !== "math" ? part.html : ""
        hoveredLink: part.hoveredLink
        font.family: Theme.fontEditor
        font.pixelSize: Theme.textBase
        color: Theme.fg
    }

    // A display formula: centred, a little larger than the text.
    BRichText {
        id: formula
        objectName: "chatSegmentMath"
        measured: true
        width: parent.width
        visible: part.kind === "math"
        html: part.kind === "math" ? part.html : ""
        horizontalAlignment: Text.AlignHCenter
        font.family: Theme.fontEditor
        font.pixelSize: Theme.textLg
        color: Theme.fg
        accessibleName: part.code
    }

    Loader {
        id: chartLoader
        width: parent.width
        height: active && item !== null ? (item as Item).implicitHeight : 0
        active: part.kind === "chart"
        visible: part.drawn
        sourceComponent: ChartView {
            objectName: "chatSegmentChart"
            width: chartLoader.width
            source: part.code
            chartData: part.rec.map.chartData
            english: part.english
            fontFamily: Theme.fontUi
            fontPixelSize: Theme.textSm
            textColor: Theme.fg
            mutedColor: Theme.fgMuted
            backgroundColor: Theme.bgElevated
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
        active: part.kind === "diagram"
        visible: part.drawn
        sourceComponent: MermaidView {
            objectName: "chatSegmentDiagram"
            width: diagramLoader.width
            source: part.code
            fontFamily: Theme.fontUi
            fontPixelSize: Theme.textSm + 1
            textColor: Theme.fg
            mutedColor: Theme.fgMuted
            backgroundColor: Theme.bgElevated
            borderColor: Theme.border
            accentColor: Theme.accent
            dark: Theme.dark
            Accessible.role: Accessible.Graphic
            Accessible.name: description
        }
    }

    HoverHandler {
        enabled: part.drawn && !Theme.touchUi
        cursorShape: Qt.PointingHandCursor
    }
}
