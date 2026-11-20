pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes
import Brulion

// CanvasEdge — one connection of a canvas (CanvasView; docs/UI.md "Canvas view"): its curve (the
// board's geometry, canvas::edgeShape: from a card's side to another's, Bézier, as Obsidian draws
// them) with its arrowheads in canvas units in the board's scaled world, its colour (a preset or
// hex; else the theme's muted line) and a ring of the accent while selected; its label is
// CanvasEdgeLabel (above the cards). No input: the board hit-tests edges itself.
Item {
    id: edge

    required property int index
    required property string edgeId
    required property string edgePath
    required property string arrows
    required property string edgeColor
    required property bool selected

    property real zoom: 1
    readonly property real px: 1 / Math.max(0.01, zoom)
    readonly property string tint: CanvasColors.hex(edgeColor, Theme.dark)
    readonly property color lineColor: tint !== "" ? tint : Theme.fgFaint
    /// The line's width on screen: 2 px at 100 %, thinner farther out, never under 1 px.
    readonly property real lineWidth: Math.max(1.2 * px, 2.5)

    z: 1

    Shape {
        id: shape
        preferredRendererType: Shape.CurveRenderer
        // The selection's ring under the line.
        ShapePath {
            strokeColor: edge.selected ? Theme.accent.alpha(0.4) : "transparent"
            strokeWidth: edge.selected ? edge.lineWidth + 6 * edge.px : 0
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            PathSvg { path: edge.selected ? edge.edgePath : "" }
        }
        ShapePath {
            strokeColor: edge.selected ? Theme.accent : edge.lineColor
            strokeWidth: edge.lineWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            PathSvg { path: edge.edgePath }
        }
        ShapePath {
            strokeColor: "transparent"
            strokeWidth: 0
            fillColor: edge.selected ? Theme.accent : edge.lineColor
            PathSvg { path: edge.arrows }
        }
    }
}
