pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Effects
import Brulion

// BShadow — elevation shadow under a rounded Rectangle (cards, menus, dialogs, toasts).
//
//   Rectangle { id: card; radius: Theme.radiusLg; ... }
//   BShadow { target: card; level: 2 }     // sibling placed before (below) the target
//
// GPU scene graphs (OpenGL/Vulkan/Metal/WebGL): a soft RectangularShadow (Qt Quick Effects).
// The software renderer has no shader effects, so a few translucent rounded rectangles
// approximate the same shadow there (offscreen screenshots, old devices).
Item {
    id: shadow

    required property Rectangle target
    property int level: 2
    property real radius: target ? target.radius : Theme.radius

    readonly property var spec: Theme.elevation(level)
    readonly property bool software: GraphicsInfo.api === GraphicsInfo.Software

    x: target ? target.x : 0
    y: target ? target.y : 0
    width: target ? target.width : 0
    height: target ? target.height : 0
    visible: level > 0 && (target ? target.visible && target.opacity > 0 : false)
    opacity: target ? target.opacity : 1
    z: target ? target.z - 0.01 : 0

    Loader {
        anchors.fill: parent
        active: !shadow.software
        sourceComponent: RectangularShadow {
            radius: shadow.radius
            blur: shadow.spec.blur
            offset: Qt.vector2d(0, shadow.spec.offsetY)
            spread: shadow.spec.spread
            color: Qt.rgba(Theme.shadow.r, Theme.shadow.g, Theme.shadow.b, shadow.spec.opacity * 2.2)
        }
    }

    // Software fallback: stacked rings fading outwards.
    Repeater {
        model: shadow.software ? Math.min(6, Math.ceil(shadow.spec.blur / 4)) : 0
        delegate: Rectangle {
            required property int index
            readonly property real grow: (index + 1) * shadow.spec.blur / (2 * Math.min(6, Math.ceil(shadow.spec.blur / 4)))
            x: -grow
            y: -grow + shadow.spec.offsetY * (0.5 + index * 0.1)
            width: shadow.width + 2 * grow
            height: shadow.height + 2 * grow
            radius: shadow.radius + grow
            color: Qt.rgba(Theme.shadow.r, Theme.shadow.g, Theme.shadow.b,
                           shadow.spec.opacity * 0.85 / Math.min(6, Math.ceil(shadow.spec.blur / 4)))
            antialiasing: true
        }
    }
}
