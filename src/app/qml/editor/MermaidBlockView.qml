pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// MermaidBlockView — a ```mermaid block (docs/SYNTAX.md §11 "Mermaid diagrams"): the diagram drawn by
// MermaidView (Brulion's own parser and layout — nothing from the note is ever run) in the theme's
// colours on a sunken panel, centred, scaled down to the column when wider (to 70 % at most, then
// it scrolls sideways). The fence lines stay
// in the file — the delegate draws the frame. A click (or Enter on the selected block) edits the
// source in place: the monospace editor of CodeBlockView with the diagram under it, drawn again as
// you type. A source Brulion cannot draw shows the source with the reason under it: a mistake (its
// line) in the danger colour, a Mermaid type Brulion does not draw yet in the muted one.
Rectangle {
    id: root

    property BlockDelegate block
    /// The source is on screen: while editing, or when there is no diagram to show.
    readonly property bool showSource: root.block.editing || diagram.status !== "ok"
    readonly property real codeLine: Theme.lineHeightPx(Theme.textSm, 1.5)
    readonly property real firstLineCenter: showSource ? sourceLoader.y + codeLine / 2
                                                       : scroller.y + Math.min(scroller.height, codeLine) / 2

    implicitHeight: (message.visible ? message.y + message.implicitHeight
                                     : scroller.visible ? scroller.y + scroller.height
                                                       : sourceLoader.y + sourceLoader.height) + Theme.space3
    height: implicitHeight
    radius: Theme.radius
    color: Theme.bgSunken
    border.width: 1
    border.color: root.block.editing ? Theme.alpha(Theme.focusRing, 0.5) : Theme.borderSubtle

    Text {
        id: label
        visible: root.showSource
        anchors.right: parent.right
        anchors.rightMargin: Theme.space3
        y: Theme.space2
        text: "mermaid"
        textFormat: Text.PlainText
        font.family: Theme.fontMono
        font.pixelSize: Theme.textXs
        color: Theme.fgFaint
    }

    // The source: read-only until clicked when shown for an error, edited in place.
    Loader {
        id: sourceLoader
        x: Theme.space4
        y: Theme.space3
        width: parent.width - 2 * Theme.space4 - label.width - Theme.space2
        height: active && item ? (item as Item).implicitHeight : 0
        active: root.showSource
        sourceComponent: InlineEditor {
            objectName: "mermaidSource"
            width: sourceLoader.width
            block: root.block
            persistent: true
            multiline: true
            highlightMode: "code"
            font.family: Theme.fontMono
            font.pixelSize: Theme.textSm
            lineHeightPx: root.codeLine
            wrapMode: TextEdit.Wrap
        }
    }

    // A diagram wider than the column is scaled down, to 70 % at most; beyond that it scrolls
    // sideways (as a wide table does).
    Flickable {
        id: scroller
        visible: diagram.status === "ok"
        x: Theme.space4
        y: root.showSource ? sourceLoader.y + sourceLoader.height + Theme.space3 : Theme.space3
        width: parent.width - 2 * Theme.space4
        height: visible ? diagram.height : 0
        contentWidth: diagram.width
        contentHeight: diagram.height
        clip: diagram.width > width
        interactive: diagram.width > width
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.HorizontalFlick

        MermaidView {
            id: diagram
            objectName: "mermaidDiagram"
            width: Math.max(scroller.width, Math.min(diagramSize.width, diagramSize.width * 0.7))
            height: implicitHeight
            source: root.block.model.editText
            fontFamily: Theme.fontUi
            fontPixelSize: Theme.textSm + 1
            textColor: Theme.fg
            mutedColor: Theme.fgMuted
            backgroundColor: Theme.bgSunken
            borderColor: Theme.border
            accentColor: Theme.accent
            dark: Theme.dark
            Accessible.role: Accessible.Graphic
            Accessible.name: description

            // Not editing: a click starts editing (Shift / Ctrl+click select blocks), a finger held
            // on it selects the block.
            TapHandler {
                id: tap
                enabled: !root.block.editing
                onTapped: if (!root.block.editor.blockTapped(root.block.index, tap.point.modifiers))
                              root.block.editor.startEditing(root.block.index, -1)
            }
            TapHandler {
                enabled: !root.block.editing && Theme.touchUi
                acceptedDevices: PointerDevice.TouchScreen
                onLongPressed: root.block.editor.longPressText(root.block.index)
            }
            HoverHandler {
                enabled: !root.block.editing && !Theme.touchUi
                cursorShape: Qt.PointingHandCursor
            }
        }
    }

    Text {
        id: message
        objectName: "mermaidMessage"
        visible: diagram.status !== "ok"
        x: Theme.space4
        y: sourceLoader.y + sourceLoader.height + Theme.space2
        width: parent.width - 2 * Theme.space4
        text: diagram.message
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        font.family: Theme.fontUi
        font.pixelSize: Theme.textXs
        color: diagram.status === "error" ? Theme.dangerText : Theme.fgMuted
    }
}
