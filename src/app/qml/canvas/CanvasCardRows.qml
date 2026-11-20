pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// CanvasCardRows — a card's rendered Markdown on a canvas (CanvasView; docs/UI.md "Canvas view"):
// the rows of CanvasCardContent — a text card's blocks, a note card's page or section — read-only
// as an embed shows them (InlineRenderer's escaped rich text in BRichText, never a note's own
// HTML): headings, lists by depth, tasks with their status, quotes, code, nested embeds named. It
// takes no input itself (the board under it does): linkAt() names the link at a point, the board's
// tap follows it.
Item {
    id: rows

    property list<var> entries
    /// The rows' text size (the board's zoom does the rest).
    property int bodySize: Theme.textEditor
    /// The rows' total height.
    readonly property real contentHeight: {
        if (entryRows.revision < 0 || entryRows.count === 0)
            return 0
        const last = entryRows.itemAt(entryRows.count - 1)
        return last ? last.y + last.height : 0
    }

    function headingSize(level: int): int {
        return level === 1 ? Theme.textLg : level === 2 ? Theme.textMd : rows.bodySize
    }

    /// The link (an InlineRenderer URL) at (x, y) of this item, "" none.
    function linkAt(x: real, y: real): string {
        for (let i = 0; i < entryRows.count; ++i) {
            const row = entryRows.itemAt(i) as CanvasCardRow
            if (row && y >= row.y && y < row.y + row.height)
                return row.linkAt(x - row.x, y - row.y)
        }
        return ""
    }

    component CanvasCardRow: Item {
        id: entry
        required property int index
        required property string kind
        required property string view
        required property string task
        required property int headingLevel
        required property int depth
        required property string title
        required property string html
        required property string raw
        required property string bullet
        y: {
            if (entryRows.revision < 0 || entry.index === 0)
                return 0
            const above = entryRows.itemAt(entry.index - 1)
            return above ? above.y + above.height : 0
        }
        readonly property bool nested: kind === "embed"
        readonly property bool rule: view === "rule"
        readonly property bool isTask: !nested && task !== ""
        readonly property bool hasBullet: !nested && bullet !== ""
        readonly property bool code: view === "code"
        readonly property int fontSize: code ? Theme.textSm : rows.headingSize(headingLevel)
        readonly property real lineHeightPx: Theme.lineHeightPx(fontSize, headingLevel > 0 ? 1.35 : 1.5)
        readonly property real indent: Math.min(6, depth) * 16
        width: rows.width
        height: nested ? 24 : rule ? Theme.space3 : text.height + (headingLevel > 0 ? Theme.space1 : 2)

        function linkAt(px: real, py: real): string {
            if (entry.nested || entry.rule)
                return ""
            return text.linkAt(px - text.x, py - text.y)
        }

        Rectangle {
            visible: entry.rule
            x: entry.indent
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - x
            height: 1
            color: Theme.border
        }
        Text {
            visible: entry.nested
            x: entry.indent
            width: parent.width - x
            anchors.verticalCenter: parent.verticalCenter
            text: entry.nested ? "↳ " + entry.title : ""
            textFormat: Text.PlainText
            font.family: Theme.fontUi
            font.pixelSize: Theme.textXs
            color: Theme.fgFaint
            elide: Text.ElideRight
        }
        Text {
            visible: entry.hasBullet
            x: entry.indent
            width: 18
            height: entry.lineHeightPx
            verticalAlignment: Text.AlignVCenter
            horizontalAlignment: Text.AlignHCenter
            text: entry.bullet
            textFormat: Text.PlainText
            font.family: Theme.fontEditor
            font.pixelSize: entry.fontSize
            color: Theme.fgMuted
        }
        TaskBox {
            visible: entry.isTask
            enabled: false
            status: entry.task || "todo"
            x: entry.indent
            y: Math.round(entry.lineHeightPx / 2 - height / 2) + 1
        }
        BRichText {
            id: text
            visible: !entry.nested && !entry.rule
            x: entry.indent + (entry.isTask ? 22 : entry.hasBullet ? 20 : 0)
            y: 1
            width: parent.width - x
            html: visible ? entry.html : ""
            font.family: entry.code ? Theme.fontMono : Theme.fontEditor
            font.pixelSize: entry.fontSize
            font.weight: entry.headingLevel > 0 ? Theme.weightSemiBold : Theme.weightNormal
            font.strikeout: entry.task === "cancelled"
            lineHeight: entry.lineHeightPx
            color: entry.task === "done" || entry.task === "cancelled" ? Theme.fgFaint
                   : entry.view === "quote" ? Theme.fgMuted : Theme.fg
            accessibleName: entry.raw
        }
    }

    Repeater {
        id: entryRows
        property int revision: 0
        onItemAdded: revision++
        onItemRemoved: revision++
        model: rows.entries
        delegate: CanvasCardRow {}
    }
}
