pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// BlockText — the text of a block: rendered rich text (escaped HTML from the core
// InlineRenderer: links, tags, chips) while the block is not edited, an InlineEditor with the raw
// Markdown while it is. Both use the same font and fixed line height, so the block does not
// move when it switches (the rendered text's height is counted in whole lines: a rich-text Text
// gives its last line the font's own height, the editor the fixed one). `centered` centres both
// (a formula block: the rendered formula and, while it is edited, its LaTeX source).
// Clicks on links are handled (page:, tag:, block:, ext:, task:,
// reminder:); a click elsewhere starts editing with the caret at the clicked position. Shift+click
// selects the blocks from the edited or selected one to this one; on touch UIs a long press selects
// the block (the editor's selection mode, where a tap moves the selection's end instead).
Item {
    id: root

    property BlockDelegate block
    /// Quote style (muted, italic) for blockquotes.
    property bool quoted: false
    /// Centres the text (a formula block: ```math, MathBlockView).
    property bool centered: false
    /// Shows and edits the block's text (BlockDelegate keeps one for text blocks; false while the
    /// block is shown by another view or hidden: no text, no inline editor).
    property bool live: true

    readonly property BlockEditor editor: block.editor
    readonly property int level: block.headingLevel
    readonly property int fontSize: editor.headingSize(level)
    readonly property int lineHeightPx: editor.headingLineHeight(level)
    readonly property real firstLineCenter: editor.firstLineCenter(level)
    readonly property bool mono: block.view === "html"
    readonly property bool editing: live && block.editing
    readonly property int align: root.centered ? Text.AlignHCenter : Text.AlignLeft

    /// The rendered text's height in whole fixed-height lines.
    readonly property real renderedHeight: lineHeightPx > 0 ? Math.max(1, Math.round(rendered.implicitHeight / lineHeightPx)) * lineHeightPx
                                                            : rendered.implicitHeight
    implicitHeight: Math.max(lineHeightPx, editing && editorLoader.item ? (editorLoader.item as Item).implicitHeight : renderedHeight)
    height: implicitHeight

    Text {
        id: rendered
        width: parent.width
        visible: !root.editing
        textFormat: Text.RichText
        text: root.live ? root.block.model.html : ""
        wrapMode: Text.Wrap
        horizontalAlignment: root.align
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
        Accessible.name: root.live ? root.block.model.plainText : ""

        HoverHandler {
            id: textHover
            enabled: !Theme.touchUi
            cursorShape: rendered.linkAt(point.position.x, point.position.y) !== "" ? Qt.PointingHandCursor : Qt.IBeamCursor
        }
    }
    // Clicks on the rendered text: on an item of their own above it, not on the Text itself. A
    // rich-text Text accepts the left mouse button and un-accepts every press it does not take
    // for a connected linkActivated, which handed the press to the editor's ListView: with a
    // mouse, clicks on text never started editing and links never opened (touch was not
    // affected). The overlay takes the press first, so the Text never sees it.
    Item {
        anchors.fill: rendered
        visible: !root.editing
        TapHandler {
            id: tap
            gesturePolicy: TapHandler.ReleaseWithinBounds
            onTapped: (eventPoint, button) => {
                // Shift+click, the touch selection mode (BlockEditor.blockTapped).
                if (root.editor.blockTapped(root.block.index, tap.point.modifiers))
                    return
                const p = eventPoint.position
                const link = rendered.linkAt(p.x, p.y)
                if (link !== "")
                    root.editor.activateLink(root.block.index, link)
                else
                    root.editor.editAt(root.block.index, p.x, p.y)
            }
        }
        // A finger held on the text: the block selection (never a menu: that is the bullet's).
        TapHandler {
            acceptedDevices: PointerDevice.TouchScreen
            enabled: Theme.touchUi
            onLongPressed: root.editor.longPressText(root.block.index)
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
                       || root.block.view === "math"
            continuation: root.block.view === "quote" || root.block.view === "callout" ? "> " : ""
            horizontalAlignment: root.align
            placeholderText: root.block.view === "math" ? "LaTeX"
                             : root.block.listItem || root.block.view === "text" ? qsTr("Type / for commands") : ""
        }
    }
}
