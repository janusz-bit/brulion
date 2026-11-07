pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window
import Brulion

// EmbedView — attachments embedded in a block (`![[photo.png]]`, `![alt](assets/a.png)`):
// images at their natural size up to the column width (rounded, with a hairline border),
// other files as chips. URLs come from EditorHost::assetUrl (file:// on desktop/Android,
// blob/data on WebAssembly). A click on a picture emits clicked(target), a click beside it
// backgroundClicked() (BlockDelegate: edit the block). SVG pictures are drawn to fit 1024×420
// with their aspect ratio (a vector picture has no pixel size of its own to cap).
// A local picture has its size from the start (the embeds' width × height, read from the file's
// header), and the view's height is a binding, not the Column's own (a positioner sets it only in
// its next polish): a block that grew when its picture had loaded moved everything below it while
// the page or the journal scrolled (docs/UI.md "Scrolling performance").
// A drawing (the embed's `drawing`: an SVG with an embedded scene, a `.excalidraw` scene, an
// Obsidian Excalidraw plugin file — docs/SYNTAX.md §2) is shown at
// its own size, never enlarged like other SVGs (it is handwriting, drawn at one unit a pixel), with
// a pen in its corner; a click on it emits drawingClicked(path): the drawing editor. Its picture
// comes from the drawing image provider in the app's theme (`url` light, `darkUrl` dark:
// EditorHost::drawingUrl) — a theme change shows the other one at once, the old one kept until
// the new one is there (retainWhileLoading), rendered at its own size × the screen's ratio.
// A picture whose format this device cannot decode (the model's `decodable`: WebP or AVIF without
// Qt's image plugins, EditorHost::canDecodePicture) is not loaded: its frame names the file and
// the format, like the picture view (ContentPictureView), instead of a load error.
// A picture from the internet (`web`: `![alt](https://…)`) is shown only from the device's cache
// (EditorHost::webPicture — QML loads nothing from the network): until it is there its frame
// names the site with "Show" (asking first is the default, AppSettings.webPicturesShown) and
// "Save to notebook", a spinner while it loads, the reason when it failed; a click on the shown
// picture opens it on its own (webOpen: WebPictureViewer, like the picture view of an
// attachment), a right-click or a long press its menu (webMenu: WebPictureMenu). Its frame grows
// to fit what it says (a phone's narrow column, a long alt text), and keeps that height while the
// picture loads, so the row changes once: when the picture is there.
// A PDF (`pdf`: `![[lecture.pdf]]`, `#page=3`; docs/UI.md "PDF viewer") is its page's thumbnail
// (PdfEmbedTile) where this build draws PDFs (`viewable`), a click emits pdfClicked(path, page):
// the PDF viewer. Without a renderer (the browser) or a thumbnail cache it is the file's chip with a
// page icon; its click opens the PDF in a new browser tab / the system's viewer (pdfClicked too:
// PageController.openPdf decides, inside the click — the browser's user gesture).
Column {
    id: root

    property list<var> embeds
    /// Reading mode (BlockEditor.readOnly): a click beside a picture edits nothing (no text cursor).
    property bool readOnly: false
    signal clicked(string target)
    /// A click on a drawing (`drawing`: an SVG with an embedded scene): the drawing editor.
    signal drawingClicked(string path)
    /// A click on a PDF's thumbnail or chip: page `page` of the PDF at vault path `path`.
    signal pdfClicked(string path, int page)
    /// A picture from the internet: "Show" / "Try again", "Save to notebook", its menu.
    signal showWeb(string url)
    signal downloadWeb(string url)
    signal webMenu(string url, string host, Item item, real x, real y)
    /// A click on a shown picture from the internet: `picture` is its embed (the cached copy).
    signal webOpen(var picture)
    signal backgroundClicked()
    /// A finger held beside the picture / chip (touch UIs).
    signal backgroundLongPressed()

    /// See BlockEditor.autoTextSafe (BChip shows its text with Text.AutoText).
    function autoTextSafe(s: string): string {
        return Js.indexOf(s, "<") >= 0 ? TextHelper.escapeHtml(s) : s
    }

    spacing: Theme.space2
    // (`revision`: itemAt() notifies nothing, a Repeater's items may come after its count; read for
    // real — qmlcachegen compiles a bare `void pictures.revision` away with the dependency.)
    height: {
        if (pictures.revision < 0)
            return 0
        let h = 0
        for (let i = 0; i < pictures.count; ++i) {
            const item = pictures.itemAt(i)
            if (item)
                h += item.height + (i > 0 ? spacing : 0)
        }
        return h
    }

    Repeater {
        id: pictures
        property int revision: 0
        onItemAdded: revision++
        onItemRemoved: revision++
        model: root.embeds
        delegate: Item {
            id: embed
            required property var modelData
            /// The embed (BlockListModel's `embeds`: an attachment, a PDF or a picture from the
            /// internet — their keys differ; a key an embed lacks reads as undefined).
            readonly property MapHolder rec: MapHolder { map: embed.modelData }
            readonly property bool web: rec.map.web === true
            /// A PDF (BlockListModel's PDF embed); shown as its thumbnail where PDFs are drawn.
            readonly property bool pdf: rec.map.pdf === true
            readonly property bool pdfTile: pdf && rec.map.viewable === true && rec.map.state !== "unsupported"
            readonly property string urlText: "" + (rec.map.url || "")
            readonly property string darkUrlText: "" + (rec.map.darkUrl || "")
            readonly property bool image: !pdf && rec.map.image === true && (web || urlText !== "")
            readonly property string state_: rec.map.state || ""
            readonly property string name: rec.map.name || ""
            readonly property string alt: rec.map.alt || ""
            readonly property string target: rec.map.target || ""
            readonly property string host: rec.map.host || ""
            readonly property string format: rec.map.format || ""
            /// A picture from the internet not shown (yet): the frame tells why and what to do.
            readonly property bool webPending: web && state_ !== "ready"
            readonly property bool decodable: rec.map.decodable !== false
            readonly property bool drawing: rec.map.drawing === true
            /// SVG: a local file by its name; a picture from the internet by the format the host
            /// found in its bytes (its URL need not end with .svg, `name` is "host › segment").
            readonly property bool scalable: drawing || (web ? Js.test(format, "^svgz?$", "i")
                                                             : Js.test(name, "\\.svgz?$", "i"))
            readonly property real fileWidth: rec.map.width || 0
            readonly property real fileHeight: rec.map.height || 0
            /// The picture's size as the Image below shows it once loaded: a raster picture's pixels,
            /// at most 2048 a side; an SVG fitted into 1024 × 420 (rendered at the screen's pixel
            /// ratio) — not known (sizeKnown false) when the model does not know the file's size
            /// (then: once loaded).
            readonly property bool sizeKnown: fileWidth > 0 && fileHeight > 0
            /// A drawing at its own size (one unit a pixel, as drawn), never enlarged.
            readonly property real drawingScale: sizeKnown ? Math.min(1, 1024 / fileWidth, 420 / fileHeight) : 1
            readonly property real dpr: scalable && Screen.devicePixelRatio > 0 ? Screen.devicePixelRatio : 1
            readonly property real boxWidth: scalable ? 1024 * dpr : 2048
            readonly property real boxHeight: scalable ? 420 * dpr : 2048
            readonly property real fitScale: !sizeKnown ? 1
                                            : scalable || fileWidth > boxWidth || fileHeight > boxHeight
                                              ? Math.min(boxWidth / fileWidth, boxHeight / fileHeight) : 1
            readonly property real knownWidth: !sizeKnown ? 0 : drawing ? Math.floor(fileWidth * drawingScale)
                                                                       : Math.floor(fileWidth * fitScale) / dpr
            readonly property real knownHeight: !sizeKnown ? 0 : drawing ? Math.floor(fileHeight * drawingScale)
                                                                        : Math.floor(fileHeight * fitScale) / dpr
            width: root.width
            height: embed.pdfTile ? pdfLoader.height : image ? picture.height : chip.height

            // Beside the picture / chip: a click there edits the block (an embed-only paragraph of
            // a document has no text line to click).
            Item {
                x: embed.pdfTile ? pdfLoader.width : embed.image ? picture.width : chip.width
                width: Math.max(0, embed.width - x)
                height: embed.height
                HoverHandler { cursorShape: root.readOnly ? Qt.ArrowCursor : Qt.IBeamCursor; enabled: !Theme.touchUi }
                TapHandler { onTapped: root.backgroundClicked() }
                TapHandler {
                    enabled: Theme.touchUi
                    acceptedDevices: PointerDevice.TouchScreen
                    onLongPressed: root.backgroundLongPressed()
                }
            }

            Rectangle {
                id: picture
                objectName: "embedPicture" // (the editor's self-test)
                visible: embed.image
                /// (Also while a drawing's other theme or version loads: the shown one stays, its size too.)
                readonly property bool loaded: (img.status === Image.Ready || (img.retainWhileLoading && img.status === Image.Loading))
                                               && img.implicitWidth > 0 && img.implicitHeight > 0
                /// The loaded picture has another shape than the file's header said (Qt's pixmap
                /// cache still had the file's old picture, replaced since): the frame takes the
                /// picture's shape, never stretching it. Normally the two agree, so nothing moves
                /// when it has loaded (an SVG's rendered size differs slightly, in its rounding).
                readonly property bool otherShape: loaded && embed.sizeKnown
                    && Math.abs(img.implicitWidth * embed.knownHeight - img.implicitHeight * embed.knownWidth)
                       > 0.02 * img.implicitWidth * embed.knownHeight
                readonly property real naturalWidth: embed.sizeKnown && !otherShape ? embed.knownWidth
                                                     : loaded ? img.implicitWidth : 320
                readonly property real naturalHeight: embed.sizeKnown && !otherShape ? embed.knownHeight
                                                      : loaded ? img.implicitHeight : 180
                // Natural size, at most the column width and 420 px high; a picture from the
                // internet not shown yet at least as high as its placeholder.
                width: Math.min(root.width, naturalWidth, 420 * naturalWidth / Math.max(1, naturalHeight))
                height: Math.max(Math.round(width * naturalHeight / Math.max(1, naturalWidth)),
                                 embed.webPending ? Math.ceil(placeholder.implicitHeight) + 2 * Theme.space3 : 0)
                radius: Theme.radius
                color: Theme.bgSunken
                border.width: 1
                border.color: Theme.borderSubtle
                clip: true

                // Decoded at most 2048 px wide/high: a phone photo (12 MP) would otherwise take
                // 48 MB (+ mipmaps) per embed. No PreserveAspectFit: with it Qt also scales small
                // images *up* to sourceSize; the frame has the picture's aspect ratio anyway.
                // SVG is rendered at sourceSize itself — both sides given, it would be stretched
                // to a 2048 px square — so it is fitted (aspect kept) into the largest box shown.
                Image {
                    id: img
                    objectName: "embedImage"
                    anchors.fill: parent
                    anchors.margins: 1
                    source: !embed.image || !embed.decodable || embed.webPending ? ""
                            : embed.drawing && Theme.dark && embed.darkUrlText !== "" ? embed.darkUrlText
                            : embed.urlText
                    // (Logical pixels for a drawing: Qt renders image providers at the screen's ratio.)
                    sourceSize: embed.drawing && embed.sizeKnown
                                ? Qt.size(Math.ceil(embed.knownWidth), Math.ceil(embed.knownHeight))
                                : embed.scalable ? Qt.size(1024, 420) : Qt.size(2048, 2048)
                    retainWhileLoading: embed.drawing
                    fillMode: embed.scalable ? Image.PreserveAspectFit : Image.Stretch
                    asynchronous: true
                    smooth: true
                    mipmap: true
                    Accessible.role: Accessible.Graphic
                    Accessible.name: embed.alt !== "" ? embed.alt : embed.name
                }
                BSpinner {
                    anchors.centerIn: parent
                    size: 20
                    running: (img.status === Image.Loading && !picture.loaded) || (embed.web && embed.state_ === "loading")
                    visible: running
                }
                // A picture from the internet waiting to be shown (asking first) or that failed.
                // While it loads only the spinner shows, but the placeholder keeps its place (a
                // hidden Column would measure 0: the frame's height follows it).
                Column {
                    id: placeholder
                    objectName: "webPicturePlaceholder"
                    readonly property bool failed: embed.state_ === "failed"
                    visible: embed.webPending
                    opacity: embed.state_ === "loading" ? 0 : 1
                    enabled: opacity > 0
                    anchors.centerIn: parent
                    width: parent.width - 2 * Theme.space3
                    spacing: Theme.space2
                    BIcon {
                        anchors.horizontalCenter: parent.horizontalCenter
                        name: placeholder.failed ? "triangle-alert" : "image"
                        size: Theme.iconSize
                        color: Theme.fgMuted
                    }
                    // The alt text, or why the picture could not be fetched.
                    Text {
                        objectName: "webPictureText"
                        visible: text !== ""
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        maximumLineCount: placeholder.failed ? 3 : 2
                        elide: Text.ElideRight
                        text: placeholder.failed ? embed.rec.map.error || "" : embed.alt
                        textFormat: Text.PlainText
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.textSm
                        color: Theme.fgMuted
                    }
                    // The site: what "Show" would contact, never elided away by a long alt text.
                    Text {
                        objectName: "webPictureHost"
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideMiddle
                        text: qsTr("Picture from %1").arg(embed.host)
                        textFormat: Text.PlainText
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.textSm
                        color: Theme.fgMuted
                    }
                    // Side by side when both fit, else one under the other — centred either way.
                    Grid {
                        objectName: "webPictureButtons"
                        anchors.horizontalCenter: parent.horizontalCenter
                        columns: showButton.implicitWidth + spacing + saveButton.implicitWidth <= parent.width ? 2 : 1
                        spacing: Theme.space2
                        horizontalItemAlignment: Grid.AlignHCenter
                        BButton {
                            id: showButton
                            objectName: "webPictureShow"
                            size: "small"
                            iconName: placeholder.failed ? "refresh-cw" : "eye"
                            text: placeholder.failed ? qsTr("Try again") : qsTr("Show")
                            onClicked: root.showWeb(embed.target)
                        }
                        BButton {
                            id: saveButton
                            objectName: "webPictureSave"
                            size: "small"
                            variant: "ghost"
                            iconName: "download"
                            text: qsTr("Save to notebook")
                            onClicked: root.downloadWeb(embed.target)
                        }
                    }
                }
                Text {
                    objectName: "embedMessage" // (the shell's self-test)
                    visible: !embed.webPending && (!embed.decodable || img.status === Image.Error)
                    anchors.centerIn: parent
                    width: parent.width - 2 * Theme.space3
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    // The picture view's own sentence (one translation for both).
                    text: !embed.decodable
                          ? embed.name + "\n"
                            + qsTranslate("ContentPictureView", "Pictures in the %1 format cannot be shown on this device.")
                              .arg(embed.format)
                          : qsTr("Cannot load %1").arg(embed.name)
                    textFormat: Text.PlainText
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.textSm
                    color: Theme.fgMuted
                }
                TapHandler {
                    id: pictureTap
                    enabled: !embed.webPending
                    acceptedButtons: Qt.LeftButton
                    onTapped: {
                        if (embed.web)
                            root.webOpen(embed.modelData)
                        else if (embed.drawing)
                            root.drawingClicked(embed.rec.map.path || "")
                        else
                            root.clicked(embed.target)
                    }
                    onLongPressed: if (embed.web) root.webMenu(embed.target, embed.host, picture,
                                                                pictureTap.point.position.x, pictureTap.point.position.y)
                }
                // A right-click on a shown picture from the internet: its menu. Not a finger (Qt
                // gives one to every TapHandler; touch: the long press above). WithinBounds: the
                // press is the picture's, the editor's right-click on the block does not open the
                // block menu as well.
                TapHandler {
                    enabled: embed.web && !embed.webPending
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
                    acceptedButtons: Qt.RightButton
                    gesturePolicy: TapHandler.WithinBounds
                    onTapped: (point) => root.webMenu(embed.target, embed.host, picture,
                                                      point.position.x, point.position.y)
                }
                HoverHandler { cursorShape: Qt.PointingHandCursor; enabled: !Theme.touchUi && !embed.webPending }
                // A drawing: a pen in the corner says a click edits it.
                Rectangle {
                    objectName: "embedDrawingBadge"
                    visible: embed.drawing && picture.loaded && picture.width >= 72 && picture.height >= 48
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.space2
                    width: Theme.iconSizeSm + 2 * Theme.space1 + 2
                    height: width
                    radius: Theme.radiusSm
                    color: Theme.alpha(Theme.bgElevated, 0.9)
                    border.width: 1
                    border.color: Theme.borderSubtle
                    BIcon {
                        anchors.centerIn: parent
                        name: "pen-tool"
                        size: Theme.iconSizeSm
                        color: Theme.fgMuted
                    }
                }
            }

            // A PDF where this build draws them: its page's thumbnail.
            Loader {
                id: pdfLoader
                active: embed.pdfTile
                visible: active
                // (No size of its own: it takes the tile's, which sizes itself by its page.)
                sourceComponent: PdfEmbedTile {
                    entry: embed.modelData
                    columnWidth: root.width
                    onClicked: (path, page) => root.pdfClicked(path, page)
                }
            }

            BChip {
                id: chip
                objectName: embed.pdf ? "pdfEmbedChip" : ""
                visible: !embed.image && !embed.pdfTile
                interactive: true
                iconName: embed.pdf ? "file-text" : embed.rec.map.image === true ? "image" : "file"
                text: root.autoTextSafe(embed.name)
                onClicked: embed.pdf ? root.pdfClicked(embed.rec.map.path || "", embed.rec.map.page || 1)
                                     : root.clicked(embed.target)
                HoverHandler {
                    id: chipHover
                    enabled: embed.pdf && !Theme.touchUi
                }
                BToolTip {
                    visible: chipHover.hovered
                    // Where the click goes (PageController::openPdf): Brulion's viewer where it
                    // draws PDFs (the chip then means only that no thumbnail could be cached),
                    // else the browser's own viewer or the system's app.
                    text: embed.rec.map.viewable === true ? qsTr("Open in the PDF viewer")
                          : Qt.platform.os === "wasm" ? qsTr("Open in a new tab") : qsTr("Open in another app")
                }
            }
        }
    }
}
