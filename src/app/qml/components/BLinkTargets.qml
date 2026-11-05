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
    readonly property bool active: shown && DeviceInfo.screenReader && Js.indexOf(root.html, "<a ") >= 0
    /// The current link items' data [{href, text, x, y, width, height}] (self-tests).
    readonly property list<var> linkData: loader.item ? (loader.item as LinkLayer).links : []

    /// A link item was activated: do what a click on `href` at (x, y) of the text item does.
    signal activated(string href, real x, real y)

    x: textItem ? textItem.x : 0
    y: textItem ? textItem.y : 0
    width: textItem ? textItem.width : 0
    height: textItem ? textItem.height : 0
    visible: active

    // Created only while active: a table cell, a query result or a Tasks row without a screen
    // reader carries this Item and an inactive Loader, nothing that lays the text out again or
    // follows its document.
    Loader {
        id: loader
        anchors.fill: parent
        active: root.active
        sourceComponent: LinkLayer {
            textItem: root.textItem
            html: root.html
            lineHeight: root.lineHeight
            maxHeight: root.maxHeight
            onActivated: (href, x, y) => root.activated(href, x, y)
        }
    }

    /// The link items over the text (BLinkTargets' properties, the item's size).
    component LinkLayer: Item {
        id: layer
        objectName: "linkLayer"

        property Item textItem
        property string html
        property real lineHeight: 0
        property real maxHeight: 0
        readonly property list<var> links: richLinks.links
        readonly property TextEdit edit: textItem as TextEdit
        readonly property Text plainText: textItem as Text
        /// A font with nothing set (Qt.font({})).
        property font defaultFont
        // Every property set first, then one layout (each setter of an enabled RichTextLinks
        // lays the text out).
        property bool ready: false

        signal activated(string href, real x, real y)

        Component.onCompleted: ready = true

        RichTextLinks {
            id: richLinks
            enabled: layer.ready
            textDocument: layer.edit !== null ? layer.edit.textDocument : null
            html: layer.edit !== null ? "" : layer.html
            // The text item's font (Bindings: a font read from a Text or TextEdit converts only to
            // a `var` in compiled code).
            font: layer.defaultFont
            width: layer.width
            lineHeight: layer.lineHeight
            alignment: layer.plainText !== null ? layer.plainText.horizontalAlignment
                       : layer.edit !== null ? layer.edit.horizontalAlignment : Text.AlignLeft
            maxHeight: layer.maxHeight
        }

        Binding {
            target: richLinks
            property: "font"
            value: layer.edit !== null ? layer.edit.font : layer.plainText.font
            when: layer.edit !== null || layer.plainText !== null
        }

        Repeater {
            model: richLinks.links
            delegate: Item {
                id: target
                required x
                required y
                required width
                required height
                /// The link's text and href (the href also for self-tests).
                required property string text
                required property string href
                objectName: "linkTarget"
                activeFocusOnTab: false
                Accessible.role: Accessible.Link
                Accessible.name: target.text
                Accessible.focusable: true
                Accessible.onPressAction: target.activate()
                Keys.onReturnPressed: target.activate()
                Keys.onEnterPressed: target.activate()
                Keys.onSpacePressed: target.activate()
                function activate(): void {
                    layer.activated(target.href, target.x + target.width / 2, target.y + target.height / 2)
                }

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
}
