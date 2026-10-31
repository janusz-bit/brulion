pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// BlockText — the text of a block: rendered rich text (escaped HTML from the core
// InlineRenderer: links, tags, chips) while the block is not edited, an InlineEditor with the raw
// Markdown while it is. Both use the same font and fixed line height, so the block does not
// move when it switches (the rendered text's height is counted in whole lines: a rich-text Text
// gives its last line the font's own height, the editor the fixed one). `centered` centres both
// (a formula block: the rendered formula and, while it is edited, its LaTeX source).
// Tinted spans (InlineRenderer's chips and #tags, inline `code`, ==highlights==) are rounded
// shapes under the text: a text with any is shown by a read-only rich-text TextEdit whose
// RichTextPills takes Qt's square backgrounds off before they are painted and gives the
// rectangles of a Repeater (pills for chips and tags, small radii for code and highlights);
// the fixed line height is the same. Any other text stays a Text (cheaper rows).
// Clicks on links are handled (page:, tag:, block:, ext:, task:,
// reminder:, day:); a click elsewhere — also on a date, 🔁 or priority chip — starts editing with
// the caret at the clicked position. Shift+click selects the blocks from the edited or selected one
// to this one; on touch UIs a long press selects the block (the editor's selection mode, where a
// tap moves the selection's end instead). A right-click or a long press on a token (📅 ⏳ 🛫 ➕ ✅
// ❌ ⏰ 🔁, a priority, a [[day]] link) opens its menu (DateChipMenu), on another link
// ([[page]], #tag, ((block)), a web address) the link's menu (LinkMenu); a right-click elsewhere
// the block menu.
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
    readonly property string html: root.live ? root.block.model.html : ""
    /// The text has a span with a background colour (InlineRenderer's chips and tags, inline code,
    /// highlights): the TextEdit with rounded shapes shows it.
    readonly property bool tinted: html.indexOf("background-color:") >= 0
    /// The item showing the rendered text: linkAt(), the anchor of the token and link menus.
    readonly property Item renderedItem: tinted && tintedLoader.item ? tintedLoader.item as Item : rendered

    /// The link at a point of the rendered text ("" = none).
    function linkAt(x: real, y: real): string {
        const edit = tintedLoader.item as TextEdit
        return root.tinted && edit ? edit.linkAt(x, y) : rendered.linkAt(x, y)
    }

    /// The rendered text's height in whole fixed-height lines.
    readonly property real renderedHeight: lineHeightPx > 0 ? Math.max(1, Math.round(renderedItem.implicitHeight / lineHeightPx)) * lineHeightPx
                                                            : renderedItem.implicitHeight
    implicitHeight: Math.max(lineHeightPx, editing && editorLoader.item ? (editorLoader.item as Item).implicitHeight : renderedHeight)
    height: implicitHeight

    Text {
        id: rendered
        width: parent.width
        visible: !root.editing && !root.tinted
        textFormat: Text.RichText
        text: root.tinted ? "" : root.html
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
    }
    // A text with tinted spans: the same text, font, colours and line height in a read-only
    // rich-text TextEdit (it exposes its document), the spans' rounded shapes drawn under it.
    Loader {
        id: tintedLoader
        width: parent.width
        active: root.tinted
        visible: !root.editing
        sourceComponent: TextEdit {
            id: tintedText
            width: tintedLoader.width
            readOnly: true
            selectByMouse: false
            selectByKeyboard: false
            activeFocusOnPress: false
            activeFocusOnTab: false
            cursorVisible: false
            textMargin: 0
            textFormat: TextEdit.RichText
            text: root.html
            wrapMode: TextEdit.Wrap
            horizontalAlignment: root.align
            font.family: root.mono ? Theme.fontMono : Theme.fontEditor
            font.pixelSize: root.mono ? Theme.textSm : root.fontSize
            font.weight: root.level > 0 ? Theme.weightSemiBold : Theme.weightNormal
            font.italic: root.quoted
            font.strikeout: root.block.taskStatus === "cancelled"
            color: root.block.closedTask ? Theme.fgFaint : root.quoted ? Theme.fgMuted : Theme.fg
            Accessible.role: Accessible.StaticText
            Accessible.name: root.live ? root.block.model.plainText : ""
            onWidthChanged: tints.refresh()

            RichTextPills {
                id: tints
                textDocument: tintedText.textDocument
                lineHeight: root.lineHeightPx
            }
            // Under the text: a child with a negative z is drawn below its parent's content.
            Repeater {
                model: tints.pills
                delegate: Rectangle {
                    required property var modelData
                    z: -1
                    x: modelData.x
                    y: modelData.y
                    width: modelData.width
                    height: modelData.height
                    radius: modelData.kind === "pill" ? height / 2 : Theme.radiusXs
                    antialiasing: true
                    color: modelData.color
                }
            }
        }
    }
    // Clicks on the rendered text: on an item of their own above it, not on the Text itself. A
    // rich-text Text accepts the left mouse button and un-accepts every press it does not take
    // for a connected linkActivated, which handed the press to the editor's ListView: with a
    // mouse, clicks on text never started editing and links never opened (touch was not
    // affected). The overlay takes the press first, so the Text (or the tinted text's TextEdit)
    // never sees it.
    Item {
        width: root.renderedItem.width
        height: root.renderedItem.height
        visible: !root.editing
        HoverHandler {
            id: textHover
            enabled: !Theme.touchUi
            // (A click on a date, 🔁 or priority chip edits the text: its menu is the right button's.)
            cursorShape: {
                const link = root.linkAt(point.position.x, point.position.y)
                return link !== "" && !root.editor.editsOnClick(link) ? Qt.PointingHandCursor : Qt.IBeamCursor
            }
        }
        TapHandler {
            id: tap
            gesturePolicy: TapHandler.ReleaseWithinBounds
            onTapped: (eventPoint, button) => {
                // Shift+click, the touch selection mode (BlockEditor.blockTapped).
                if (root.editor.blockTapped(root.block.index, tap.point.modifiers))
                    return
                const p = eventPoint.position
                const link = root.linkAt(p.x, p.y)
                if (link !== "" && !root.editor.editsOnClick(link))
                    root.editor.activateLink(root.block.index, link)
                else
                    root.editor.editAt(root.block.index, p.x, p.y)
            }
        }
        // A right-click: a token's menu, a link's menu, else the block menu (as the list's handler
        // opens it; WithinBounds takes the press, so that one does not open it too).
        TapHandler {
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
            acceptedButtons: Qt.RightButton
            gesturePolicy: TapHandler.WithinBounds
            onTapped: (eventPoint) => {
                const p = eventPoint.position
                const link = root.linkAt(p.x, p.y)
                if (!root.editor.openDateMenu(root.block.index, link, root.renderedItem, p.x, p.y)
                        && !root.editor.openBlockLinkMenu(root.block.index, link, root.renderedItem, p.x, p.y))
                    root.editor.openBlockMenuAt(root.block.index, eventPoint.scenePosition)
            }
        }
        // A finger held on the text: the block selection (never the block menu: that is the
        // bullet's) — on a token or a link, outside the selection mode, its menu.
        TapHandler {
            id: hold
            acceptedDevices: PointerDevice.TouchScreen
            enabled: Theme.touchUi
            onLongPressed: {
                const p = hold.point.position
                const link = root.linkAt(p.x, p.y)
                if (!root.editor.selectionMode
                        && (root.editor.openDateMenu(root.block.index, link, root.renderedItem, p.x, p.y)
                            || root.editor.openBlockLinkMenu(root.block.index, link, root.renderedItem, p.x, p.y)))
                    return
                root.editor.longPressText(root.block.index)
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
                       || root.block.view === "math"
            continuation: root.block.view === "quote" || root.block.view === "callout" ? "> " : ""
            horizontalAlignment: root.align
            placeholderText: root.block.view === "math" ? "LaTeX"
                             : root.block.listItem || root.block.view === "text" ? qsTr("Type / for commands") : ""
        }
    }
}
