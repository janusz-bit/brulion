pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// CanvasCard — one node of a canvas (CanvasView; docs/UI.md "Canvas view"), placed in canvas units
// in the board's scaled world: a text card's Markdown, a note's page (or its section, or block), a
// picture, a drawing, a PDF's first page, another canvas's picture, another file's name and icon,
// a web address (no page is fetched), a group's frame with its label and background picture. Its
// colour tints it (presets as Obsidian shows them, or the file's hex colour). Zoomed far out (or
// too small on screen) a card shows only its colour and title, large. It takes no input: the board
// under it does (linkAt() for a tap's link, scrollBy() for the wheel over a selected card).
Item {
    id: card
    objectName: "canvasCard"

    // The model's row (CanvasNodeModel).
    required property int index
    required property string nodeId
    required property string kind
    required property real nodeX
    required property real nodeY
    required property real nodeWidth
    required property real nodeHeight
    required property string cardColor
    required property string cardText
    required property string cardFile
    required property string subpath
    required property string cardUrl
    required property string cardLabel
    required property string background
    required property string backgroundStyle
    required property bool selected
    required property string fileKind
    required property string filePath
    required property string cardTitle
    required property int order

    property CanvasEditor board: null
    property CanvasCards cards: null
    /// The card whose text is being edited (its content hides under the editor).
    property bool editing: false

    readonly property real zoom: board ? board.zoom : 1
    readonly property bool isGroup: kind === "group"
    readonly property bool isText: kind !== "group" && kind !== "file" && kind !== "link"
    /// Shown in full: zoomed in enough, and large enough on screen to read.
    readonly property bool detailed: board !== null && board.detailed && width * zoom >= 48 && height * zoom >= 28
    /// A picture of a file (a picture, a drawing, a canvas) shows at any zoom while it is a few
    /// pixels large (decoded small: sourceSize follows the size on screen).
    readonly property bool pictured: kind === "file" && width * zoom >= 24 && height * zoom >= 24
                                     && (fileKind === "picture" || fileKind === "drawing" || fileKind === "canvas")
    readonly property bool shown: detailed || pictured
    /// The colour of a preset or a hex colour ("" none).
    readonly property string tint: CanvasColors.hex(cardColor, Theme.dark)
    readonly property color tintColor: tint !== "" ? tint : "transparent"
    readonly property color frameColor: tint !== "" ? tint : (isGroup ? Theme.borderStrong : Theme.border)
    readonly property list<var> noRows: []
    /// One screen pixel in canvas units.
    readonly property real px: 1 / Math.max(0.01, zoom)
    /// The content scrolled up by this much (a note or text longer than the card).
    property real scrollY: 0
    readonly property real contentHeight: card.kind === "file" ? noteRows.contentHeight + noteHeader.height
                                                               : textRows.contentHeight

    x: nodeX
    y: nodeY
    width: nodeWidth
    height: nodeHeight
    // Groups below every card (as Obsidian draws them), the cards in their order above the edges.
    z: isGroup ? order / 100000 : 2 + order / 100000

    /// The link (an InlineRenderer URL) at (x, y) of the card, "" none.
    function linkAt(px: real, py: real): string {
        if (!card.detailed)
            return ""
        if (textRows.visible)
            return textRows.linkAt(px - textRows.x, py - textRows.y + card.scrollY)
        if (noteRows.visible)
            return noteRows.linkAt(px - noteRows.x, py - noteRows.y + card.scrollY)
        return ""
    }
    /// The wheel over the selected card: its content scrolls (within its height).
    function scrollBy(dy: real): void {
        const room = card.contentHeight - (card.height - 2 * Theme.space3)
        card.scrollY = Math.max(0, Math.min(Math.max(0, room), card.scrollY + dy))
    }
    readonly property bool overflows: card.detailed && card.contentHeight > card.height - Theme.space3
    onOverflowsChanged: if (card.board) card.board.setScrollable(card.nodeId, card.overflows)

    // ---- a group ------------------------------------------------------------------------------
    Rectangle {
        id: groupFrame
        visible: card.isGroup
        anchors.fill: parent
        radius: Theme.radiusLg
        color: card.tint !== "" ? card.tintColor.alpha(0.08) : Theme.bgSunken.alpha(0.55)
        border.width: (card.selected ? 2.5 : 1.5) * card.px
        border.color: card.selected ? Theme.accent : card.frameColor
        clip: true
        Image {
            visible: card.isGroup && card.background !== ""
            anchors.fill: parent
            autoTransform: true // a photo stored sideways, upright by its EXIF orientation
            source: visible && card.cards ? card.cards.pictureUrl(card.background) : ""
            asynchronous: true
            fillMode: card.backgroundStyle === "repeat" ? Image.Tile
                      : card.backgroundStyle === "ratio" ? Image.PreserveAspectFit : Image.PreserveAspectCrop
            // Decoded at about the size shown, as a card's picture below (a few steps, not every
            // zoom): a 12 MP photo behind a group took 48 MB and as much again as a texture. A
            // repeated picture keeps its own pixels (0: no size asked for) — its tiles are that large.
            sourceSize.width: card.backgroundStyle === "repeat" ? 0
                : Math.min(2048, Math.pow(2, Math.ceil(Math.log(Math.max(64, card.width * card.zoom * 1.5)) / Math.LN2)))
            opacity: 0.85
        }
    }
    // The label above the frame (larger when zoomed out, so it reads from afar).
    Text {
        visible: card.isGroup && card.cardLabel !== ""
        x: 4 * card.px
        y: -height - 4 * card.px
        width: Math.max(card.width, 200)
        text: card.cardLabel
        textFormat: Text.PlainText
        elide: Text.ElideRight
        font.family: Theme.fontUi
        font.weight: Theme.weightSemiBold
        font.pixelSize: Math.min(64, Math.max(Theme.textMd, 13 * card.px))
        color: card.tint !== "" ? card.tint : Theme.fgMuted
    }

    // ---- a card -----------------------------------------------------------------------------------
    // Its shadow and the selection's ring around it.
    Rectangle {
        visible: !card.isGroup && card.selected
        anchors.fill: parent
        anchors.margins: -4 * card.px
        radius: Theme.radiusLg + 4 * card.px
        color: "transparent"
        border.width: 2 * card.px
        border.color: Theme.accent.alpha(0.35)
    }
    Rectangle {
        id: frame
        visible: !card.isGroup
        anchors.fill: parent
        radius: Theme.radiusLg
        color: Theme.bgElevated
        border.width: (card.selected ? 2.5 : card.tint !== "" ? 2 : 1) * card.px
        border.color: card.selected ? Theme.accent : card.frameColor
        clip: true

        // The colour's tint over the card.
        Rectangle {
            anchors.fill: parent
            radius: frame.radius
            visible: card.tint !== ""
            color: card.tintColor.alpha(Theme.dark ? 0.14 : 0.08)
        }

        // Zoomed out: the title alone, as large as the card allows.
        Text {
            visible: !card.shown && !card.isGroup
            anchors.fill: parent
            anchors.margins: Math.min(parent.width, parent.height) * 0.08
            text: card.cardTitle !== "" ? card.cardTitle : card.kind === "text" ? "…" : card.cardFile
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            elide: Text.ElideRight
            maximumLineCount: 3
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font.family: Theme.fontUi
            font.weight: Theme.weightSemiBold
            font.pixelSize: Math.max(8, Math.min(parent.height * 0.3, 18 * card.px))
            fontSizeMode: Text.Fit
            minimumPixelSize: 6
            color: Theme.fgMuted
        }

        // What the card shows.
        CanvasCardContent {
            id: content
            cards: card.cards
            active: card.shown
            dark: Theme.dark
            kind: card.isText ? "text" : card.kind === "file" ? card.fileKind : ""
            text: card.isText ? card.cardText : ""
            file: card.filePath
            subpath: card.subpath
        }

        // A text card's Markdown.
        CanvasCardRows {
            id: textRows
            visible: card.detailed && card.isText && !card.editing
            x: Theme.space3
            y: Theme.space2 - card.scrollY
            width: parent.width - 2 * Theme.space3
            height: contentHeight
            entries: visible ? content.rows : card.noRows
        }

        // A note: its name, then its page (or section, or block).
        Rectangle {
            id: noteHeader
            visible: card.detailed && card.kind === "file" && (card.fileKind === "note" || card.fileKind === "missing")
            width: parent.width
            height: visible ? 30 : 0
            y: -card.scrollY
            color: "transparent"
            BIcon {
                x: Theme.space3
                anchors.verticalCenter: parent.verticalCenter
                name: card.fileKind === "missing" ? "circle-alert" : "file-text"
                size: Theme.iconSizeSm
                color: Theme.fgFaint
            }
            Text {
                x: Theme.space3 + Theme.iconSizeSm + Theme.space2
                width: parent.width - x - Theme.space3
                anchors.verticalCenter: parent.verticalCenter
                text: content.title !== "" ? content.title : card.cardTitle
                textFormat: Text.PlainText
                elide: Text.ElideRight
                font.family: Theme.fontUi
                font.pixelSize: Theme.textSm
                font.weight: Theme.weightMedium
                color: Theme.fgMuted
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: card.px
                color: Theme.borderSubtle
            }
        }
        CanvasCardRows {
            id: noteRows
            visible: card.detailed && card.kind === "file" && card.fileKind === "note"
            x: Theme.space3
            y: noteHeader.height + Theme.space2 - card.scrollY
            width: parent.width - 2 * Theme.space3
            height: contentHeight
            entries: visible ? content.rows : card.noRows
        }
        Text {
            visible: card.detailed && card.kind === "file"
                     && (card.fileKind === "missing" || (card.fileKind === "note" && content.rows.length === 0))
            x: Theme.space3
            y: noteHeader.height + Theme.space2
            width: parent.width - 2 * Theme.space3
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
            font.family: Theme.fontUi
            font.pixelSize: Theme.textSm
            color: Theme.fgFaint
            text: {
                if (card.fileKind === "missing")
                    return qsTr("“%1” is not in the notebook.").arg(card.cardFile)
                switch (content.message) {
                case "missingSection": return qsTr("There is no such section on this page.")
                case "missingBlock": return qsTr("This block was not found.")
                case "missingPage": return qsTr("This page does not exist yet.")
                default: return qsTr("Nothing here yet.")
                }
            }
        }

        // A picture, a drawing, a canvas, a PDF's first page.
        Image {
            id: picture
            visible: card.shown && ("" + content.pictureUrl) !== ""
            anchors.fill: parent
            anchors.margins: card.fileKind === "pdf" ? Theme.space3 : 0
            anchors.bottomMargin: card.fileKind === "pdf" ? 28 : 0
            autoTransform: true // a photo stored sideways, upright by its EXIF orientation
            source: visible ? content.pictureUrl : ""
            asynchronous: true
            fillMode: card.fileKind === "picture" ? Image.PreserveAspectCrop : Image.PreserveAspectFit
            // Decoded at about the size shown (a few steps, not every zoom).
            sourceSize.width: Math.min(2048, Math.pow(2, Math.ceil(Math.log(Math.max(64, card.width * card.zoom * 1.5)) / Math.LN2)))
            mipmap: true
        }
        BSpinner {
            visible: card.detailed && card.fileKind === "pdf" && content.pdfState === "loading"
            anchors.centerIn: parent
        }

        // A PDF's name, another file's icon and name.
        Row {
            visible: card.detailed && card.kind === "file"
                     && (card.fileKind === "pdf" || card.fileKind === "file" || card.fileKind === "picture"
                         || card.fileKind === "drawing" || card.fileKind === "canvas")
                     && (card.fileKind !== "picture" || picture.status !== Image.Ready)
                     && (card.fileKind !== "drawing" && card.fileKind !== "canvas" || picture.status !== Image.Ready)
            x: Theme.space3
            y: card.fileKind === "file" ? (frame.height - height) / 2 : frame.height - height - Theme.space2
            spacing: Theme.space2
            BIcon {
                anchors.verticalCenter: parent.verticalCenter
                name: card.fileKind === "pdf" ? "file-text" : card.fileKind === "canvas" ? "layout-dashboard" : "paperclip"
                size: Theme.iconSize
                color: Theme.fgMuted
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(0, frame.width - 2 * Theme.space3 - Theme.iconSize - Theme.space2)
                text: card.fileKind === "pdf" && content.pdfPages > 0
                      ? qsTr("%1 · %n page(s)", "", content.pdfPages).arg(card.cardTitle) : card.cardTitle
                textFormat: Text.PlainText
                elide: Text.ElideMiddle
                font.family: Theme.fontUi
                font.pixelSize: Theme.textSm
                color: Theme.fgMuted
            }
        }

        // A web address: its host large, the address under it (nothing is fetched).
        Column {
            visible: card.detailed && card.kind === "link"
            anchors.verticalCenter: parent.verticalCenter
            x: Theme.space4
            width: parent.width - 2 * Theme.space4
            spacing: Theme.space1
            BIcon {
                name: "globe"
                size: Theme.iconSizeLg
                color: card.tint !== "" ? card.tint : Theme.link
            }
            Text {
                width: parent.width
                text: Js.replaceRegExp(Js.replaceRegExp(card.cardUrl, "^[a-zA-Z][a-zA-Z0-9+.-]*://", "", ""), "/.*$", "", "")
                textFormat: Text.PlainText
                elide: Text.ElideRight
                font.family: Theme.fontUi
                font.pixelSize: Theme.textMd
                font.weight: Theme.weightSemiBold
                color: Theme.fg
            }
            Text {
                width: parent.width
                text: card.cardUrl
                textFormat: Text.PlainText
                elide: Text.ElideMiddle
                font.family: Theme.fontUi
                font.pixelSize: Theme.textXs
                color: Theme.link
            }
        }
    }

    // A scroll bar while the content is longer than the card.
    Rectangle {
        visible: card.overflows && card.selected
        x: card.width - 6 * card.px
        y: Theme.space1 + (card.height - 2 * Theme.space1 - height) * Math.min(1, card.scrollY / Math.max(1, card.contentHeight - card.height))
        width: 3 * card.px
        height: Math.max(16 * card.px, (card.height - 2 * Theme.space1) * card.height / Math.max(1, card.contentHeight))
        radius: width / 2
        color: Theme.scrollbar
    }
}
