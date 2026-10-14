pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// BProgressBar — thin determinate or indeterminate progress line.
//
//   BProgressBar { value: done; to: total }              // sync: 12 of 40 files
//   BProgressBar { indeterminate: true }                  // waiting for the server
//
// `tone`: "default" (link blue) | "accent" | "success" | "warning" | "danger".
// Indeterminate: a sliding segment (a gentle pulse with Theme.reducedMotion).
T.ProgressBar {
    id: control

    property string tone: "default"
    property int thickness: 4

    readonly property color fillColor: {
        switch (tone) {
        case "accent": return Theme.accent
        case "success": return Theme.success
        case "warning": return Theme.warning
        case "danger": return Theme.danger
        default: return Theme.link
        }
    }

    implicitWidth: 200
    implicitHeight: thickness

    Accessible.role: Accessible.ProgressBar

    contentItem: Item {
        clip: true

        Rectangle {
            visible: !control.indeterminate
            width: control.visualPosition * parent.width
            height: parent.height
            radius: height / 2
            color: control.fillColor
            Behavior on width { NumberAnimation { duration: Theme.duration; easing.type: Theme.easing } }
        }

        Rectangle {
            id: runner
            visible: control.indeterminate
            width: Theme.reducedMotion ? parent.width : parent.width * 0.32
            height: parent.height
            radius: height / 2
            color: control.fillColor

            SequentialAnimation on x {
                running: control.indeterminate && control.visible && !Theme.reducedMotion
                loops: Animation.Infinite
                NumberAnimation {
                    from: -runner.width
                    to: control.width
                    duration: 1300
                    easing.type: Easing.InOutQuad
                }
            }
            SequentialAnimation on opacity {
                running: control.indeterminate && control.visible && Theme.reducedMotion
                loops: Animation.Infinite
                NumberAnimation { from: 0.35; to: 1; duration: 1200 }
                NumberAnimation { from: 1; to: 0.35; duration: 1200 }
            }
        }
    }

    background: Rectangle {
        implicitWidth: 200
        implicitHeight: control.thickness
        radius: height / 2
        color: Qt.rgba(Theme.fg.r, Theme.fg.g, Theme.fg.b, Theme.dark ? 0.10 : 0.08)
    }
}
