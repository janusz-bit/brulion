pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// BRichText — rendered note text outside the block editor's own blocks (escaped rich text from
// the core InlineRenderer, never a note's own HTML): linked references (ContentRefRow), Tasks
// (TasksRow), embeds (TransclusionView), table cells (TableBlockView), query results
// (QueryBlockView) and the link preview (BLinkPreview). As in the editor's blocks (BlockText),
// tinted spans — #tags and the date / reminder / done / recurrence chips, inline `code`,
// ==highlights== — are rounded shapes, not Qt's square backgrounds, and the text keeps a fixed
// line height.
//
// A plain text (no tint, within the line limit) is a QML Text: the cheap row. A text with tinted
// spans, or one longer than `maximumLineCount` lines, is a read-only rich-text TextEdit whose
// RichTextPills sets the HTML, takes the square backgrounds off before they are painted, cuts the
// text after `maximumLineCount` lines with "…" (QML Text ignores maximumLineCount and elide for
// rich text) and gives the rectangles drawn under it. Either way the item has its final height
// when it is created (docs/UI.md "Scrolling performance").
//
// It takes no pointer input: the caller's handlers ask `linkAt(x, y)` (its own coordinates) and
// put the link under the pointer in `hoveredLink` (underlined: TextHelper.hoveredLinkHtml). For
// screen readers, BLinkTargets { textItem: rich.textItem; html: rich.html; lineHeight: … }.
//
//   BRichText { width: parent.width; html: model.html; hoveredLink: row.hoveredLink
//               font.family: Theme.fontEditor; font.pixelSize: Theme.textBase
//               lineHeight: 22; maximumLineCount: 4; color: Theme.fg }
Item {
    id: root

    /// The rendered rich text (InlineRenderer's escaped HTML).
    property string html
    /// The link to underline ("" = none).
    property string hoveredLink
    /// The fixed line height in pixels (0: the font's; then no line limit).
    property real lineHeight: 0
    /// Lines shown at most (0: all); a longer text ends with "…".
    property int maximumLineCount: 0
    property font font: Qt.font({ family: Theme.fontUi, pixelSize: Theme.textBase })
    property color color: Theme.fg
    property color linkColor: Theme.link
    property int horizontalAlignment: Text.AlignLeft
    /// What a screen reader says for the text ("": the text is ignored by it, the caller's item
    /// speaks for it).
    property string accessibleName

    /// The shown rich text: the hovered link underlined.
    readonly property string shownHtml: TextHelper.hoveredLinkHtml(root.html, root.hoveredLink)
    /// The text has a span with a background colour (chips, tags, inline code, highlights).
    readonly property bool tinted: shownHtml.indexOf("background-color:") >= 0
    /// A plain text longer than the line limit (measured by the Text, which shows every line).
    readonly property bool overflows: !tinted && maximumLineCount > 0 && lineHeight > 0
                                      && plain.implicitHeight > (maximumLineCount + 0.5) * lineHeight
    /// Shown by the TextEdit (tinted or cut), not by the Text.
    readonly property bool rich: tinted || overflows
    /// The item showing the text (BLinkTargets' textItem, the anchor of a link's menu).
    readonly property Item textItem: rich && richLoader.item ? richLoader.item as Item : plain
    /// The text was cut after maximumLineCount lines.
    readonly property bool truncated: rich && richLoader.item ? (richLoader.item as RichEdit).cut : false

    /// The link at (x, y) of this item ("" = none).
    function linkAt(x: real, y: real): string {
        const edit = richLoader.item as TextEdit
        return root.rich && edit ? edit.linkAt(x, y) : plain.linkAt(x, y)
    }

    implicitWidth: plain.implicitWidth
    // In whole fixed-height lines (a rich-text Text gives its last line the font's own height).
    implicitHeight: root.lineHeight > 0 ? Math.max(1, Math.round(textItem.implicitHeight / root.lineHeight)) * root.lineHeight
                                        : textItem.implicitHeight

    Text {
        id: plain
        width: root.width
        visible: !root.rich
        // Measured while it overflows (the TextEdit shows it); empty when the TextEdit has a tint.
        text: root.tinted ? "" : root.shownHtml
        textFormat: Text.RichText
        wrapMode: Text.Wrap
        horizontalAlignment: root.horizontalAlignment
        font: root.font
        color: root.color
        linkColor: root.linkColor
        lineHeightMode: root.lineHeight > 0 ? Text.FixedHeight : Text.ProportionalHeight
        lineHeight: root.lineHeight > 0 ? root.lineHeight : 1
        Accessible.role: Accessible.StaticText
        Accessible.name: root.accessibleName
        Accessible.ignored: root.accessibleName === "" || root.rich
    }

    Loader {
        id: richLoader
        width: root.width
        active: root.rich
        visible: active
        sourceComponent: RichEdit {}
    }

    /// The tinted or cut text: a read-only TextEdit, its text set by RichTextPills.
    component RichEdit: TextEdit {
        id: edit
        readonly property bool cut: tints.truncated
        width: root.width
        // No input of its own (the caller's handlers decide what a press does).
        enabled: false
        readOnly: true
        selectByMouse: false
        selectByKeyboard: false
        activeFocusOnPress: false
        activeFocusOnTab: false
        cursorVisible: false
        textMargin: 0
        textFormat: TextEdit.RichText
        wrapMode: TextEdit.Wrap
        horizontalAlignment: root.horizontalAlignment
        font: root.font
        color: root.color
        Accessible.role: Accessible.StaticText
        Accessible.name: root.accessibleName
        Accessible.ignored: root.accessibleName === ""

        RichTextPills {
            id: tints
            textDocument: edit.textDocument
            html: root.shownHtml
            lineHeight: root.lineHeight
            maximumLineCount: root.maximumLineCount
            font: edit.font
            width: edit.width
        }
        // Under the text: a child with a negative z is drawn below its parent's content.
        Repeater {
            model: tints.pills
            delegate: Rectangle {
                id: tintShape
                required property var modelData
                z: -1
                x: modelData.x
                y: modelData.y
                width: modelData.width
                height: modelData.height
                radius: modelData.kind === "pill" ? height / 2 : Theme.radiusXs
                antialiasing: true
                // A page's app icon in front of a link (RichTextPills kind "icon"): the icon, no shape.
                color: modelData.kind === "icon" ? "transparent" : modelData.color
                BIcon {
                    anchors.centerIn: parent
                    name: tintShape.modelData.kind === "icon" ? tintShape.modelData.icon : ""
                    size: Math.round(Math.min(parent.width, parent.height) * 0.9)
                    color: tintShape.modelData.color
                }
            }
        }
    }
}
