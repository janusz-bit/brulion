pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes
import Brulion

// BSpinner — indeterminate activity indicator: a rotating arc over a faint track.
//
//   BSpinner { size: 24; running: model.loading }
//   BSpinner { calm: true }   // for minutes of work: a tick ten times a second
//
// Rotation slows down (but does not stop — it carries meaning) with Theme.reducedMotion. A smooth
// spin draws the window anew at the screen's rate; `calm` turns it in steps, ten frames a second —
// for an indicator shown while a model on the device writes, whose threads it would take a fast
// core from (docs/PERFORMANCE.md "The model on the device").
Item {
    id: spinner

    property real size: Theme.iconSize
    property color color: Theme.fgMuted
    property bool running: visible
    /// Turns in steps, ten frames a second, instead of at the screen's rate.
    property bool calm: false
    property real strokeWidth: Math.max(1.5, size / 9)

    implicitWidth: size
    implicitHeight: size

    Accessible.role: Accessible.Animation
    Accessible.name: qsTr("Loading")

    Shape {
        id: shape
        anchors.fill: parent
        antialiasing: true
        preferredRendererType: Shape.CurveRenderer

        readonly property real r: (spinner.size - spinner.strokeWidth) / 2
        readonly property real c: spinner.size / 2

        ShapePath {
            strokeColor: spinner.color.alpha(0.22)
            strokeWidth: spinner.strokeWidth
            fillColor: "transparent"
            PathAngleArc {
                centerX: shape.c
                centerY: shape.c
                radiusX: shape.r
                radiusY: shape.r
                startAngle: 0
                sweepAngle: 360
            }
        }
        ShapePath {
            strokeColor: spinner.color
            strokeWidth: spinner.strokeWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            PathAngleArc {
                centerX: shape.c
                centerY: shape.c
                radiusX: shape.r
                radiusY: shape.r
                startAngle: -90
                sweepAngle: 100
            }
        }

        RotationAnimator on rotation {
            from: 0
            to: 360
            duration: Theme.reducedMotion ? 2400 : 850
            loops: Animation.Infinite
            running: spinner.running && spinner.visible && !spinner.calm
        }
    }

    Timer {
        interval: 100
        repeat: true
        running: spinner.running && spinner.visible && spinner.calm
        onTriggered: shape.rotation = (shape.rotation + (Theme.reducedMotion ? 15 : 36)) % 360
    }
}
