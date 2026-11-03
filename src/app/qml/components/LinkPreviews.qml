pragma Singleton
pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// LinkPreviews — the hover preview of [[page]] and ((block)) links (docs/UI.md "Link previews"):
// one popup per window (BLinkPreview), created the first time it opens. Rendered text tells it
// where the mouse pointer rests:
//
//   onPointChanged: LinkPreviews.hover(textItem, link, sourcePath, Workspace, x, y)  // link "" = none
//   onHoveredChanged: if (!hovered) LinkPreviews.leave(textItem)
//
// A page: or block: link under the pointer for `openDelay` ms opens its preview under the pointer
// (above it near the window's bottom); it stays while the pointer is on the link or on the popup
// and closes `closeDelay` ms after it left both, on a press elsewhere, when the text it belongs to
// goes away. Another link closes it and opens its own after the delay. Touch UIs
// show none (no hover; a long press is the link's menu). A click on a link inside follows it
// (activate()), the header opens the previewed page.
QtObject {
    id: hub

    /// How long the pointer rests on a link before its preview opens, and how long the preview
    /// stays after the pointer left both the link and the preview (ms).
    property int openDelay: 600
    property int closeDelay: 350

    /// The text, link and its note the next preview is for (while openTimer runs).
    property Item pendingItem: null
    property var pending: null

    /// The open preview: the text its link is in (null when it goes away), the link, the note
    /// the link is written in, the host it is read through.
    property Item anchorItem: null
    property string href
    property string fromPath
    property EditorHost host: null
    /// The pointer is on the previewed link / on the preview.
    property bool overLink: false
    property bool overPopup: false
    /// The popup (BLinkPreview), created in the first window a preview opens in.
    property var popup: null
    readonly property bool shown: popup !== null && popup.visible

    /// The link's scheme is one previews are shown for.
    function previewable(href: string): bool {
        return (href.startsWith("page:") && href.length > 5) || (href.startsWith("block:") && href.length > 6)
    }

    /// The pointer rests at (x, y) of `item` on the link `href` ("" or a link without a preview:
    /// none) of text written in `fromPath`, read through `host` (Workspace, the editor's host).
    function hover(item: Item, href: string, fromPath: string, host: EditorHost, x: real, y: real) {
        if (!item || Theme.touchUi)
            return
        if (!hub.previewable(href) || !host) {
            hub.leave(item)
            return
        }
        if (hub.shown && hub.anchorItem === item && hub.href === href) {
            hub.overLink = true
            hub.closeTimer.stop()
            if (hub.pendingItem === item) {
                hub.openTimer.stop()
                hub.pendingItem = null
            }
            return
        }
        if (hub.pendingItem === item && hub.pending && hub.pending.href === href && hub.openTimer.running) {
            hub.pending.x = x // (the pointer moves along the link: the timer goes on)
            hub.pending.y = y
            return
        }
        hub.pendingItem = item
        hub.pending = { href: href, fromPath: fromPath, host: host, x: x, y: y }
        hub.openTimer.restart()
        if (hub.anchorItem === item)
            hub.overLink = false // another link of the same text
        hub.closeLater()
    }
    /// The pointer left `item` (or rests on no previewable link of it).
    function leave(item: Item) {
        if (item && hub.pendingItem === item) {
            hub.openTimer.stop()
            hub.pendingItem = null
            hub.pending = null
        }
        if (item && hub.anchorItem === item) {
            hub.overLink = false
            hub.closeLater()
        }
    }
    /// The pointer entered or left the preview.
    function setOverPopup(over: bool) {
        hub.overPopup = over
        if (over)
            hub.closeTimer.stop()
        else
            hub.closeLater()
    }
    function closeLater() {
        if (hub.shown && !hub.overLink && !hub.overPopup)
            hub.closeTimer.restart()
    }
    property bool closing: false
    function close() {
        if (hub.closing)
            return
        hub.closing = true
        hub.openTimer.stop()
        hub.closeTimer.stop()
        hub.pendingItem = null
        hub.pending = null
        hub.overLink = false
        hub.overPopup = false
        hub.anchorItem = null
        if (hub.popup)
            hub.popup.close()
        hub.closing = false
    }
    /// Follow `url` (a link inside the preview, written in `path`; the header: the previewed
    /// link) as a click on it does; `newPane`: the side panel. The preview closes.
    function activate(url: string, path: string, newPane: bool): void {
        if (hub.popup)
            hub.popup.openLink(url, path, newPane)
        hub.close()
    }
    /// Open the preview of `pendingItem`'s link now (the timer; tests).
    function openNow() {
        hub.openTimer.stop()
        const item = hub.pendingItem
        const p = hub.pending
        hub.pendingItem = null
        hub.pending = null
        if (!item || !p || !item.visible)
            return
        // The window's root item: the popup lives in the window of the text it previews.
        let root = item
        while (root.parent)
            root = root.parent
        if (hub.popup && hub.popup.parent !== root) {
            hub.popup.close()
            hub.popup.destroy()
            hub.popup = null
        }
        if (!hub.popup)
            hub.popup = hub.popupComponent.createObject(root, { parent: root })
        if (!hub.popup)
            return
        hub.closeTimer.stop()
        hub.anchorItem = item
        hub.href = p.href
        hub.fromPath = p.fromPath
        hub.host = p.host
        hub.overLink = true
        hub.overPopup = false
        const at = item.mapToItem(root, p.x, p.y)
        hub.popup.show(at.x, at.y)
    }

    // The text under the open preview went away (a row scrolled out, the page closed): so does it.
    onAnchorItemChanged: if (!hub.anchorItem && !hub.closing && hub.popup && hub.popup.visible) hub.close()

    property Timer openTimer: Timer {
        interval: hub.openDelay
        onTriggered: hub.openNow()
    }
    property Timer closeTimer: Timer {
        interval: hub.closeDelay
        onTriggered: {
            if (!hub.overLink && !hub.overPopup)
                hub.close()
        }
    }
    property Component popupComponent: Component {
        BLinkPreview {
            href: hub.href
            fromPath: hub.fromPath
            host: hub.host
            onPointerInsideChanged: hub.setOverPopup(pointerInside)
            onActivated: (url, path, newPane) => hub.activate(url, path, newPane)
            onClosed: {
                if (hub.anchorItem !== null)
                    hub.close()
            }
        }
    }
}
