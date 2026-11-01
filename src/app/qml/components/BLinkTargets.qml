pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// BLinkTargets — the links of a rendered rich text (InlineRenderer's HTML in a Text or a
// read-only TextEdit) as accessible items of their own: one Accessible.Link per anchor — each
// path part of [[A/B/C]] too —, named by its text, over the anchor's first line; its press action
// (a TalkBack double tap, Orca's / NVDA's activate, UiAutomator's click) and Enter / Space with
// the keyboard focus do what a click on the link does. The items exist only while a screen reader
// or another accessibility client is on (DeviceInfo.screenReader): otherwise nothing is laid out
// and the text stays one static text for them (docs/UI.md "Links in rendered text").
//
//   BLinkTargets { textItem: rendered; html: root.html; lineHeight: 26
//                  onActivated: (href, x, y) => root.editor.activateLink(row, href) }
Item {
    id: root

    /// The Text or TextEdit showing the rich text (sits over it, the same geometry).
    property Item textItem
    /// The rich text it shows (for a Text: laid out again here; a TextEdit's document is read).
    property string html
    /// The text's fixed line height (0: the font's).
    property real lineHeight: 0
    /// Only the links starting above this are items (an elided row; 0: all).
    property real maxHeight: 0
    /// The text is shown (false while its raw Markdown is edited: no link items).
    property bool shown: true
    /// The link items exist (shown, a screen reader is on and the text has links).
    readonly property bool active: shown && DeviceInfo.screenReader && root.html.indexOf("<a ") >= 0
    /// The current link items' data [{href, text, x, y, width, height}] (self-tests).
    readonly property var linkData: richLinks.links

    /// A link item was activated: do what a click on `href` at (x, y) of the text item does.
    signal activated(string href, real x, real y)

    x: textItem ? textItem.x : 0
    y: textItem ? textItem.y : 0
    width: textItem ? textItem.width : 0
    height: textItem ? textItem.height : 0
    visible: active

    RichTextLinks {
        id: richLinks
        enabled: root.active
        textDocument: root.textItem instanceof TextEdit ? (root.textItem as TextEdit).textDocument : null
        html: root.textItem instanceof TextEdit ? "" : root.html
        font: root.textItem instanceof TextEdit ? (root.textItem as TextEdit).font
              : root.textItem instanceof Text ? (root.textItem as Text).font : Qt.font({})
        width: root.width
        lineHeight: root.lineHeight
        alignment: root.textItem instanceof Text ? (root.textItem as Text).horizontalAlignment
                   : root.textItem instanceof TextEdit ? (root.textItem as TextEdit).horizontalAlignment : Text.AlignLeft
        maxHeight: root.maxHeight
    }

    Repeater {
        model: root.active ? richLinks.links : []
        delegate: Item {
            id: target
            required property var modelData
            objectName: "linkTarget"
            x: modelData.x
            y: modelData.y
            width: modelData.width
            height: modelData.height
            activeFocusOnTab: false
            Accessible.role: Accessible.Link
            Accessible.name: modelData.text
            Accessible.focusable: true
            Accessible.onPressAction: target.activate()
            Keys.onReturnPressed: target.activate()
            Keys.onEnterPressed: target.activate()
            Keys.onSpacePressed: target.activate()
            function activate() {
                root.activated(target.modelData.href, target.x + target.width / 2, target.y + target.height / 2)
            }
            /// The href (self-tests).
            readonly property string href: modelData.href

            // Which link the keyboard or screen-reader focus is on.
            Rectangle {
                anchors.fill: parent
                anchors.margins: -2
                visible: target.activeFocus
                color: "transparent"
                radius: Theme.radiusXs
                border.color: Theme.focusRing
                border.width: Theme.focusRingWidth
            }
        }
    }
}
