pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// CodeBlockView — a fenced code block: monospace text on a sunken panel with the language as a
// small label. The fence lines are not shown; the content is edited in place (persistent
// InlineEditor, read-only until clicked, so nothing jumps). Enter inserts a line break, Tab a tab.
Rectangle {
    id: root

    property BlockDelegate block
    readonly property real firstLineCenter: code.y + Theme.lineHeightPx(Theme.textSm, 1.5) / 2

    implicitHeight: code.y + code.implicitHeight + Theme.space3
    height: implicitHeight
    radius: Theme.radius
    color: Theme.bgSunken
    border.width: 1
    border.color: root.block.editing ? Theme.alpha(Theme.focusRing, 0.5) : Theme.borderSubtle

    Text {
        id: label
        visible: root.block.model.lang !== ""
        anchors.right: parent.right
        anchors.rightMargin: Theme.space3
        y: Theme.space2
        text: root.block.model.lang
        textFormat: Text.PlainText
        font.family: Theme.fontMono
        font.pixelSize: Theme.textXs
        color: Theme.fgFaint
    }

    InlineEditor {
        id: code
        x: Theme.space4
        y: Theme.space3
        width: parent.width - 2 * Theme.space4 - (label.visible ? label.width + Theme.space2 : 0)
        block: root.block
        persistent: true
        multiline: true
        highlightMode: "code"
        font.family: Theme.fontMono
        font.pixelSize: Theme.textSm
        lineHeightPx: Theme.lineHeightPx(Theme.textSm, 1.5)
        wrapMode: TextEdit.Wrap
    }
}
