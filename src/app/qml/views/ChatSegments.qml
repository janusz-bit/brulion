pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// ChatSegments — an AI message with a drawing, its parts one under another (docs/UI.md "AI chat";
// ChatMessageModel's segments, ChatSegment each): text, a chart, a Mermaid diagram, a display
// formula — in the bubble's width rules (naturalWidth: its widest part; a chart takes the whole
// bubble). The answer being written shows its finished blocks so too (ChatView's chatPartialSettled:
// rendered again only when a block is finished). A tap on a drawing opens it large
// (ChatDrawingViewer: openAt()); the message's menu copies its source (drawingSourceAt()). Takes no
// pointer input itself: the bubble's handlers ask linkAt(), openAt() and drawingSourceAt() with
// points of this item.
//
//   ChatSegments { width: …; segments: message.segments; hoveredLink: …; english: … }
Column {
    id: root

    /// ChatMessageModel's segments ([{kind, html, code, source, chartData}]).
    property list<var> segments
    property string hoveredLink
    property bool english
    /// The widest part's own width.
    readonly property real naturalWidth: root.widest()

    spacing: 10 // (a paragraph's gap, as renderMessage's)

    function widest(): real {
        let w = 0
        for (let i = 0; i < parts.count; ++i) {
            const seg = parts.itemAt(i) as ChatSegment
            if (seg !== null)
                w = Math.max(w, seg.naturalWidth)
        }
        return w
    }
    /// The part at (x, y) of this item, null where there is none.
    function partAt(x: real, y: real): ChatSegment {
        for (let i = 0; i < parts.count; ++i) {
            const seg = parts.itemAt(i) as ChatSegment
            if (seg !== null && y >= seg.y && y < seg.y + seg.height && x >= 0 && x < root.width)
                return seg
        }
        return null
    }
    /// The link at (x, y) of this item ("" none).
    function linkAt(x: real, y: real): string {
        const seg = root.partAt(x, y)
        return seg !== null ? seg.linkAt(x - seg.x, y - seg.y) : ""
    }
    /// The source of the drawing at (x, y) as written ("" no drawing there).
    function drawingSourceAt(x: real, y: real): string {
        const seg = root.partAt(x, y)
        return seg !== null && seg.drawn ? seg.source : ""
    }
    /// Opens the drawing at (x, y) large; false when there is none.
    function openAt(x: real, y: real): bool {
        const seg = root.partAt(x, y)
        if (seg === null || !seg.drawn)
            return false
        viewerLoader.active = true
        const viewer = viewerLoader.item as ChatDrawingViewer
        if (viewer === null)
            return false
        viewer.show(seg.modelData)
        return true
    }

    Repeater {
        id: parts
        model: root.segments
        delegate: ChatSegment {
            width: root.width
            hoveredLink: root.hoveredLink
            english: root.english
        }
    }

    // The drawing large, made the first time one is opened.
    Loader {
        id: viewerLoader
        active: false
        sourceComponent: ChatDrawingViewer {
            english: root.english
        }
    }
}
