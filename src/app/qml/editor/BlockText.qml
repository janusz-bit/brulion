pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// BlockText — the text of a block: rendered rich text (escaped HTML from the core
// InlineRenderer: links, tags, chips) while the block is not edited, an InlineEditor with the raw
// Markdown while it is. Both use the same font and fixed line height, so the block does not
// move when it switches. Clicks on links are handled (page:, tag:, block:, ext:, task:,
// reminder:); a click elsewhere starts editing with the caret at the clicked position.
Item {
    id: root

    property BlockDelegate block
    /// Quote style (muted, italic) for blockquotes.
    property bool quoted: false

    readonly property BlockEditor editor: block.editor
    readonly property int level: block.headingLevel
    readonly property int fontSize: editor.headingSize(level)
    readonly property int lineHeightPx: editor.headingLineHeight(level)
    readonly property real firstLineCenter: editor.firstLineCenter(level)
    readonly property bool mono: block.view === "html"
    readonly property bool editing: block.editing

    implicitHeight: Math.max(lineHeightPx, editing && editorLoader.item ? (editorLoader.item as Item).implicitHeight : rendered.implicitHeight)
    height: implicitHeight

    Text {
        id: rendered
        width: parent.width
        visible: !root.editing
        textFormat: Text.RichText
        text: root.block.model.html
        wrapMode: Text.Wrap
        font.family: root.mono ? Theme.fontMono : Theme.fontEditor
        font.pixelSize: root.mono ? Theme.textSm : root.fontSize
        font.weight: root.level > 0 ? Theme.weightSemiBold : Theme.weightNormal
        font.italic: root.quoted
        font.strikeout: root.block.taskStatus === "cancelled"
        lineHeightMode: Text.FixedHeight
        lineHeight: root.lineHeightPx
        color: root.block.closedTask ? Theme.fgFaint : root.quoted ? Theme.fgMuted : Theme.fg
        linkColor: Theme.link
        Accessible.role: Accessible.StaticText
        Accessible.name: root.block.model.plainText

        HoverHandler {
            id: textHover
            enabled: !Theme.touchUi
            cursorShape: rendered.linkAt(point.position.x, point.position.y) !== "" ? Qt.PointingHandCursor : Qt.IBeamCursor
        }
        TapHandler {
            enabled: !root.editing
            gesturePolicy: TapHandler.ReleaseWithinBounds
            onTapped: (eventPoint, button) => {
                const p = eventPoint.position
                const link = rendered.linkAt(p.x, p.y)
                if (link !== "")
                    root.editor.activateLink(root.block.index, link)
                else
                    root.editor.editAt(root.block.index, p.x, p.y)
            }
        }
    }

    Loader {
        id: editorLoader
        width: parent.width
        active: root.editing
        sourceComponent: InlineEditor {
            width: editorLoader.width
            block: root.block
            font.family: root.mono ? Theme.fontMono : Theme.fontEditor
            font.pixelSize: root.mono ? Theme.textSm : root.fontSize
            font.weight: root.level > 0 ? Theme.weightSemiBold : Theme.weightNormal
            lineHeightPx: root.lineHeightPx
            multiline: root.block.view === "html" || root.block.view === "quote" || root.block.view === "callout"
            continuation: root.block.view === "quote" || root.block.view === "callout" ? "> " : ""
            placeholderText: root.block.listItem || root.block.view === "text" ? qsTr("Type / for commands") : ""
        }
    }
}
