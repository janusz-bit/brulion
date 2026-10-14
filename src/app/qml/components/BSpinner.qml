pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes
import Brulion

// BSpinner — indeterminate activity indicator: a rotating arc over a faint track.
//
//   BSpinner { size: 24; running: model.loading }
//
// Rotation slows down (but does not stop — it carries meaning) with Theme.reducedMotion.
Item {
    id: spinner

    property real size: Theme.iconSize
    property color color: Theme.fgMuted
    property bool running: visible
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

        ShapePath {
            strokeColor: Qt.rgba(spinner.color.r, spinner.color.g, spinner.color.b, 0.22)
            strokeWidth: spinner.strokeWidth
            fillColor: "transparent"
            PathAngleArc {
                centerX: spinner.size / 2
                centerY: spinner.size / 2
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
                centerX: spinner.size / 2
                centerY: spinner.size / 2
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
            running: spinner.running && spinner.visible
        }
    }
}
