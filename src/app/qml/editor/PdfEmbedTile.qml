pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// PdfEmbedTile — a PDF embedded in a block (`![[wykład.pdf]]`, `![[wykład.pdf#page=3]]`; docs/UI.md
// "PDF viewer"): the thumbnail of its page on a white page with a hairline frame (white in the dark
// theme too: a page is paper), and a caption row — a page icon, the file's name, its page count, the
// embedded page. Sized from the page's shape (A4 portrait 297 × 420, a 16:9 slide 480 × 270, at most
// 420 high and the column's width) — known from the start once the thumbnail is cached, so the
// block does not jump when it arrives. The picture is the cached PNG (EditorHost::pdfThumbnail:
// file://), a spinner while it is drawn, a lock and the reason for a PDF that cannot be shown. A tap
// emits clicked(path, page): the PDF viewer (EmbedView → BlockDelegate → PageController.openPdf).
//
//   PdfEmbedTile { entry: modelData; columnWidth: root.width; onClicked: (path, page) => … }
Item {
    id: tile

    objectName: "pdfEmbedTile" // (the shell's self-test)

    /// The embed (BlockListModel's PDF embed): {path, name, page, state, url, width, height, pages,
    /// pageWidth, pageHeight, error, locked}.
    property var entry: ({})
    property real columnWidth: 480

    signal clicked(string path, int page)

    readonly property string state_: entry.state || "loading"
    readonly property int pages: entry.pages || 0
    readonly property int page: entry.page || 1
    /// The page's height / width: from its size in points, else the thumbnail's pixels, else A4.
    readonly property real aspect: {
        if ((entry.pageWidth || 0) > 0 && (entry.pageHeight || 0) > 0)
            return Math.min(32, Math.max(1 / 32, entry.pageHeight / entry.pageWidth))
        if ((entry.width || 0) > 0 && (entry.height || 0) > 0)
            return entry.height / entry.width
        return 1.414
    }
    readonly property real frameWidth: {
        const w = Math.min(columnWidth, aspect >= 1 ? 297 : 480)
        return Math.round(w * aspect) > 420 ? Math.floor(420 / aspect) : w
    }
    readonly property real frameHeight: Math.min(420, Math.round(frameWidth * aspect))
    /// The thumbnail is drawn (the shell's self-test).
    readonly property bool loaded: thumb.status === Image.Ready && thumb.implicitWidth > 0

    implicitWidth: frame.width
    implicitHeight: frame.height + Theme.space1 + caption.height

    Accessible.role: Accessible.Button
    //: A PDF embedded in a note, for screen readers: its name, the page shown, the page count
    Accessible.name: qsTr("PDF %1, page %2 of %3").arg(entry.name || "").arg(tile.page).arg(tile.pages > 0 ? tile.pages : "?")
    Accessible.onPressAction: tile.clicked(entry.path || "", tile.page)

    Rectangle {
        id: frame
        objectName: "pdfEmbedFrame"
        width: tile.frameWidth
        height: tile.frameHeight
        color: "#ffffff" // a page is white, also in the dark theme
        border.width: 1
        border.color: Theme.borderSubtle
        radius: Theme.radiusSm
        clip: true

        Image {
            id: thumb
            objectName: "pdfEmbedImage"
            anchors.fill: parent
            anchors.margins: 3
            source: tile.state_ === "ready" ? (tile.entry.url || "") : ""
            // Decoded at the shown size (× the screen's ratio, by Qt).
            sourceSize: Qt.size(Math.ceil(width), Math.ceil(height))
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            smooth: true
            mipmap: true
        }
        BSpinner {
            anchors.centerIn: parent
            size: 20
            color: "#5f6368"
            running: tile.state_ === "loading" || (tile.state_ === "ready" && thumb.status === Image.Loading)
            visible: running
        }
        Column {
            objectName: "pdfEmbedFailure"
            visible: tile.state_ === "failed"
            anchors.centerIn: parent
            width: parent.width - 2 * Theme.space3
            spacing: Theme.space2
            BIcon {
                anchors.horizontalCenter: parent.horizontalCenter
                name: tile.entry.locked === true ? "lock" : "triangle-alert"
                size: Theme.iconSize
                color: "#5f6368"
            }
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
                text: tile.entry.error || ""
                textFormat: Text.PlainText
                font.family: Theme.fontUi
                font.pixelSize: Theme.textSm
                color: "#5f6368" // on the white page in both themes
            }
        }
    }

    Row {
        id: caption
        objectName: "pdfEmbedCaption"
        y: frame.height + Theme.space1
        width: frame.width
        height: Math.max(Theme.iconSizeSm, nameText.implicitHeight)
        spacing: Theme.space1

        BIcon {
            anchors.verticalCenter: parent.verticalCenter
            name: "file-text"
            size: Theme.iconSizeSm
            color: Theme.fgMuted
        }
        Text {
            id: nameText
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(0, Math.min(implicitWidth, caption.width - Theme.iconSizeSm - details.implicitWidth
                                                       - 2 * caption.spacing))
            text: tile.entry.name || ""
            textFormat: Text.PlainText // a file name from anywhere is no markup
            elide: Text.ElideMiddle
            font.family: Theme.fontUi
            font.pixelSize: Theme.textSm
            color: Theme.fgMuted
        }
        Text {
            id: details
            anchors.verticalCenter: parent.verticalCenter
            text: {
                const parts = []
                if (tile.pages > 0)
                    parts.push(qsTr("%n page(s)", "", tile.pages))
                if (tile.page > 1 && (tile.pages === 0 || tile.page <= tile.pages))
                    //: The page of a PDF an embed shows ("s. 3")
                    parts.push(qsTr("p. %1").arg(tile.page))
                return parts.length > 0 ? "· " + parts.join(" · ") : ""
            }
            textFormat: Text.PlainText
            font.family: Theme.fontUi
            font.pixelSize: Theme.textSm
            color: Theme.fgMuted
        }
    }

    TapHandler {
        acceptedButtons: Qt.LeftButton
        onTapped: tile.clicked(tile.entry.path || "", tile.page)
    }
    HoverHandler {
        cursorShape: Qt.PointingHandCursor
        enabled: !Theme.touchUi
    }
}
