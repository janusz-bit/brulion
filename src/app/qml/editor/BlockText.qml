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
// rectangles BTintShapes draws (pills for chips and tags, small radii for code and highlights);
// the fixed line height is the same. Any other text stays a Text (cheaper rows).
// Clicks on links are handled (page:, tag:, block:, ext:, task:,
// reminder:, day:); a click elsewhere — also on a date, 🔁 or priority chip — starts editing with
// the caret at the clicked position. Shift+click selects the blocks from the edited or selected one
// to this one; on touch UIs a long press selects the block (the editor's selection mode, where a
// tap moves the selection's end instead). A right-click or a long press on a token (📅 ⏳ 🛫 ➕ ✅
// ❌ ⏰ 🔁, a priority, a [[day]] link) opens its menu (DateChipMenu), on another link
// ([[page]], a path part of [[Folder/Page]], #tag, ((block)), a web address) the link's menu
// (LinkMenu); a right-click elsewhere the block menu. The link under the mouse — or under the
// finger or button while it presses — is underlined (hoveredLink, TextHelper.hoveredLinkHtml: the
// shown text only), so each path part of [[A/B/C]] shows what a click opens; with a screen reader
// on every link is an accessible item of its own (BLinkTargets). The mouse pointer resting on a
// [[page]] or ((block)) link (or a day's link) shows its preview (LinkPreviews).
Item {
    id: root

    final property BlockDelegate block: blockItem as BlockDelegate
    /// The block as an Item: what BlockDelegate gives (a file cannot give its own root to a
    /// property of its own type in compiled code); `block` follows it unless set directly.
    property Item blockItem
    /// Quote style (muted, italic) for blockquotes.
    property bool quoted: false
    /// Centres the text (a formula block: ```math, MathBlockView).
    property bool centered: false
    /// Shows and edits the block's text (BlockDelegate keeps one for text blocks; false while the
    /// block is shown by another view or hidden: no text, no inline editor).
    property bool live: true

    final property BlockEditor editor: block.editor
    /// The editor's controller and find bar (typed here: looked into below).
    readonly property PageController controller: editor.controller
    readonly property NoteFind find: editor.find
    readonly property int level: block.shownHeadingLevel
    readonly property int fontSize: editor.headingSize(level)
    readonly property int lineHeightPx: editor.headingLineHeight(level)
    readonly property real firstLineCenter: editor.firstLineCenter(level)
    readonly property bool mono: block.view === "html"
    readonly property bool editing: live && block.editing
    readonly property int align: root.centered ? Text.AlignHCenter : Text.AlignLeft
    readonly property string html: root.live ? root.block.rowData.html : ""
    /// The rendered text with the find bar's matches tinted (docs/UI.md "Find and replace").
    readonly property string findHtml: {
        const find = root.find
        return find !== null && find.appliedQuery !== "" && root.live
                ? TextHelper.findHighlightedHtml(root.html, find.appliedQuery, find.matchCase, find.matchDiacritics,
                                                 find.wholeWord, root.editor.findHitColor.toString())
                : root.html
    }
    /// The text has a span with a background colour (InlineRenderer's chips and tags, inline code,
    /// highlights): the TextEdit with rounded shapes shows it.
    readonly property bool tinted: Js.indexOf(findHtml, "background-color:") >= 0
    /// The item showing the rendered text: linkAt(), the anchor of the token and link menus.
    readonly property Item renderedItem: tinted && tintedEdit !== null ? tintedEdit : rendered
    /// The tinted text's TextEdit (null while the text has no tints).
    readonly property TextEdit tintedEdit: tintedLoader.item !== null ? (tintedLoader.item as TintedText).edit : null

    /// The link at a point of the rendered text ("" = none).
    function linkAt(x: real, y: real): string {
        const edit = root.tintedEdit
        return root.tinted && edit ? edit.linkAt(x, y) : rendered.linkAt(x, y)
    }
    /// The link under the mouse, or under the finger / button while it presses ("" = none): shown
    /// underlined. Not the task checkbox nor a chip a click edits (dates, 🔁, priorities).
    property string hoveredLink: ""
    /// Where a right-click or a long press on the text was (in the text, in the scene): kept here
    /// while the menus are asked for (compiled code reads no event values after other calls).
    property point menuAt
    property point menuScene
    function setMenuPoints(at: point, scene: point): void {
        menuAt = at
        menuScene = scene
    }
    /// The shown rich text: the rendered HTML with hoveredLink underlined.
    readonly property string shownHtml: TextHelper.hoveredLinkHtml(root.findHtml, root.hoveredLink)
    function trackLink(x: real, y: real): void {
        const link = root.editing ? "" : root.linkAt(x, y)
        const hovered = link !== "" && !Js.startsWith(link, "task:") && !root.editor.editsOnClick(link) ? link : ""
        if (hovered !== root.hoveredLink)
            root.hoveredLink = hovered
    }
    onEditingChanged: {
        root.hoveredLink = ""
        LinkPreviews.leave(root)
    }
    /// The link whose preview the resting pointer asks for (LinkPreviews: page: and block:; a day
    /// of the journal, a day: chip here, by the page it names).
    readonly property string previewLink: Js.startsWith(root.hoveredLink, "day:") && root.controller !== null
                                          ? root.controller.dateChipUrl(root.block.index, root.hoveredLink)
                                          : root.hoveredLink

    /// The rendered text's height in whole fixed-height lines.
    readonly property real renderedHeight: lineHeightPx > 0 ? Math.max(1, Math.round(renderedItem.implicitHeight / lineHeightPx)) * lineHeightPx
                                                            : renderedItem.implicitHeight
    implicitHeight: Math.max(lineHeightPx, editing && editorLoader.item ? (editorLoader.item as Item).implicitHeight : renderedHeight)
    height: implicitHeight

    Text {
        id: rendered
        width: parent.width
        // Its implicit height, but never 0: a rich text's height growing from 0 makes QQuickText lay
        // it out a second time (QQuickText::geometryChange, Qt 6.11); from 1 px its first layout
        // gives it the rest without another.
        height: Math.max(1, implicitHeight)
        visible: !root.editing && !root.tinted
        textFormat: Text.RichText
        text: root.tinted ? "" : root.shownHtml
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
        Accessible.name: root.live ? root.block.plainText : ""
    }
    // A text with tinted spans: the same text, font, colours and line height in a read-only
    // rich-text TextEdit (it exposes its document), the spans' rounded shapes drawn under it.
    Loader {
        id: tintedLoader
        width: parent.width
        active: root.tinted
        visible: !root.editing
        sourceComponent: TintedText {}
    }
    // The TextEdit inside an item of its own: a Loader reads its item's implicit width, which makes
    // a wrapped TextEdit lay its text out twice more (unwrapped for the width, then wrapped again).
    component TintedText: Item {
        readonly property TextEdit edit: tintedText
        width: tintedLoader.width
        implicitHeight: tintedText.implicitHeight
        TextEdit {
            id: tintedText
            width: parent.width
            readOnly: true
            selectByMouse: false
            selectByKeyboard: false
            activeFocusOnPress: false
            activeFocusOnTab: false
            cursorVisible: false
            textMargin: 0
            textFormat: TextEdit.RichText
            text: root.shownHtml
            wrapMode: TextEdit.Wrap
            horizontalAlignment: root.align
            font.family: root.mono ? Theme.fontMono : Theme.fontEditor
            font.pixelSize: root.mono ? Theme.textSm : root.fontSize
            font.weight: root.level > 0 ? Theme.weightSemiBold : Theme.weightNormal
            font.italic: root.quoted
            font.strikeout: root.block.taskStatus === "cancelled"
            color: root.block.closedTask ? Theme.fgFaint : root.quoted ? Theme.fgMuted : Theme.fg
            Accessible.role: Accessible.StaticText
            Accessible.name: root.live ? root.block.plainText : ""
            onWidthChanged: tints.refresh()

            RichTextPills {
                id: tints
                textDocument: tintedText.textDocument
                lineHeight: root.lineHeightPx
            }
            BTintShapes { pills: tints }
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
            onPointChanged: {
                root.trackLink(point.position.x, point.position.y)
                // The link under the resting pointer: its preview (docs/UI.md "Link previews").
                const controller = root.controller
                LinkPreviews.hover(root, tap.pressed ? "" : root.previewLink, controller ? controller.path : "",
                                   controller ? controller.host : null, point.position.x, point.position.y)
            }
            onHoveredChanged: {
                if (!hovered) {
                    LinkPreviews.leave(root)
                    if (!tap.pressed)
                        root.hoveredLink = ""
                }
            }
        }
        TapHandler {
            id: tap
            gesturePolicy: TapHandler.ReleaseWithinBounds
            // The press feedback (phones have no hover): the link under the finger is underlined
            // until it lifts or the press turns into a flick.
            onPressedChanged: {
                if (pressed)
                    root.trackLink(tap.point.position.x, tap.point.position.y)
                else if (!textHover.hovered)
                    root.hoveredLink = ""
            }
            onTapped: (eventPoint, button) => {
                const x = eventPoint.position.x
                const y = eventPoint.position.y
                // Shift+click, the touch selection mode (BlockEditor.blockTapped).
                if (root.editor.blockTappedWith(root.block.index, tap.point.modifiers))
                    return
                const link = root.linkAt(x, y)
                if (link !== "" && !root.editor.editsOnClick(link))
                    root.editor.activateLink(root.block.index, link)
                else
                    root.editor.editAt(root.block.index, x, y)
            }
        }
        // A middle click on a page, tag or block link: that link in a new tab (OpenIn; the app's
        // shell — Ctrl+click picks blocks here, docs/UI.md "Tabs, split view and windows").
        TapHandler {
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            acceptedButtons: Qt.MiddleButton
            enabled: OpenIn.tabs
            onTapped: (eventPoint) => {
                const link = root.linkAt(eventPoint.position.x, eventPoint.position.y)
                root.editor.openLinkInTab(root.block.index, link)
            }
        }
        // A right-click: a token's menu, a link's menu, else the block menu (as the list's handler
        // opens it; WithinBounds takes the press, so that one does not open it too).
        TapHandler {
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
            acceptedButtons: Qt.RightButton
            gesturePolicy: TapHandler.WithinBounds
            onTapped: (eventPoint) => {
                root.setMenuPoints(eventPoint.position, eventPoint.scenePosition)
                const link = root.linkAt(root.menuAt.x, root.menuAt.y)
                if (!root.editor.openDateMenu(root.block.index, link, root.renderedItem, root.menuAt.x, root.menuAt.y)
                        && !root.editor.openBlockLinkMenu(root.block.index, link, root.renderedItem, root.menuAt.x,
                                                          root.menuAt.y))
                    root.editor.openBlockMenuAt(root.block.index, root.menuScene)
            }
        }
        // A finger held on the text: the block selection (never the block menu: that is the
        // bullet's) — on a token or a link, outside the selection mode, its menu.
        TapHandler {
            id: hold
            acceptedDevices: PointerDevice.TouchScreen
            enabled: Theme.touchUi
            onLongPressed: {
                root.menuAt = hold.point.position
                const link = root.linkAt(root.menuAt.x, root.menuAt.y)
                if (!root.editor.selectionMode
                        && (root.editor.openDateMenu(root.block.index, link, root.renderedItem, root.menuAt.x, root.menuAt.y)
                            || root.editor.openBlockLinkMenu(root.block.index, link, root.renderedItem, root.menuAt.x,
                                                             root.menuAt.y)))
                    return
                root.editor.longPressText(root.block.index)
            }
        }
    }

    // Screen readers: every link of the shown text an accessible link item (a press does what a
    // click does; a date chip opens its menu).
    BLinkTargets {
        textItem: root.renderedItem
        html: root.html
        lineHeight: root.lineHeightPx
        shown: root.live && !root.editing
        onActivated: (href, x, y) => {
            if (root.editor.editsOnClick(href))
                root.editor.openDateMenu(root.block.index, href, root.renderedItem, x, y)
            else
                root.editor.activateLink(root.block.index, href)
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
