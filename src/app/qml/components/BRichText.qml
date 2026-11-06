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
    property font font: Theme.fontUiBase
    property color color: Theme.fg
    property color linkColor: Theme.link
    property int horizontalAlignment: Text.AlignLeft
    /// What a screen reader says for the text ("": the text is ignored by it, the caller's item
    /// speaks for it).
    property string accessibleName

    /// The shown rich text: the hovered link underlined.
    readonly property string shownHtml: TextHelper.hoveredLinkHtml(root.html, root.hoveredLink)
    /// The text has a span with a background colour (chips, tags, inline code, highlights).
    readonly property bool tinted: Js.indexOf(shownHtml, "background-color:") >= 0
    /// A plain text longer than the line limit (measured by the Text, which shows every line).
    readonly property bool overflows: !tinted && maximumLineCount > 0 && lineHeight > 0
                                      && plain.implicitHeight > (maximumLineCount + 0.5) * lineHeight
    /// Shown by the TextEdit (tinted or cut), not by the Text.
    readonly property bool rich: tinted || overflows
    /// The item showing the text (BLinkTargets' textItem, the anchor of a link's menu).
    readonly property Item textItem: rich && richEdit !== null ? richEdit : plain
    /// The text was cut after maximumLineCount lines.
    readonly property bool truncated: rich && richEdit !== null ? richEdit.cut : false
    /// The TextEdit (null while the Text shows the text).
    readonly property RichEdit richEdit: richLoader.item !== null ? (richLoader.item as RichBox).edit : null

    /// The link at (x, y) of this item ("" = none).
    function linkAt(x: real, y: real): string {
        const edit = root.richEdit
        return root.rich && edit ? edit.linkAt(x, y) : plain.linkAt(x, y)
    }

    implicitWidth: plain.implicitWidth
    // In whole fixed-height lines (a rich-text Text gives its last line the font's own height).
    implicitHeight: root.lineHeight > 0 ? Math.max(1, Math.round(textItem.implicitHeight / root.lineHeight)) * root.lineHeight
                                        : textItem.implicitHeight

    Text {
        id: plain
        width: root.width
        // Its implicit height, but never 0: a rich text's height growing from 0 makes QQuickText lay
        // it out a second time (QQuickText::geometryChange, Qt 6.11); from 1 px its first layout
        // gives it the rest without another.
        height: Math.max(1, implicitHeight)
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
        sourceComponent: RichBox {}
    }

    // The TextEdit inside an item of its own: a Loader reads its item's implicit width, which makes
    // a wrapped TextEdit lay its text out twice more (unwrapped for the width, then wrapped again).
    component RichBox: Item {
        readonly property RichEdit edit: richBoxEdit
        width: root.width
        implicitHeight: richBoxEdit.implicitHeight
        RichEdit { id: richBoxEdit }
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
        BTintShapes { pills: tints }
    }
}
