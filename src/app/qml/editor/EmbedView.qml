pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// EmbedView — attachments embedded in a block (`![[photo.png]]`, `![alt](assets/a.png)`):
// images at their natural size up to the column width, on the page's colour (a hairline frame
// with AppSettings.pictureFrames — or the note's own `picture-frames` option, PageController.pictureFrames —
// or while the picture is not shown),
// other files as chips. URLs come from EditorHost::assetUrl (file:// on desktop/Android,
// blob/data on WebAssembly). A click on a picture emits clicked(target), a click beside it
// backgroundClicked() (BlockDelegate: edit the block). SVG pictures are drawn to fit 1024×420
// with their aspect ratio (a vector picture has no pixel size of its own to cap).
// A local picture has its size from the start (the embeds' width × height, read from the file's
// header), and the view's height is a binding, not the Column's own (a positioner sets it only in
// its next polish): a block that grew when its picture had loaded moved everything below it while
// the page or the journal scrolled (docs/UI.md "Scrolling performance").
// A picture holds its pixels only in or near the window's view (NearView on its frame: a page is
// created whole, with every picture's frame), decoded at the size its frame shows it
// (PictureDecode), not at the file's: docs/UI.md "Pictures hold their pixels only near the view".
// Its frame keeps its place either way.
// A drawing (the embed's `drawing`: an SVG with an embedded scene, a `.excalidraw` scene, an
// Obsidian Excalidraw plugin file — docs/SYNTAX.md §2) is shown at
// its own size, never enlarged like other SVGs (it is handwriting, drawn at one unit a pixel), with
// a pen in its corner; a click on it emits drawingClicked(path): the drawing editor. Its picture
// comes from the drawing image provider in the app's theme (`url` light, `darkUrl` dark:
// EditorHost::drawingUrl) — a theme change shows the other one at once, the old one kept until
// the new one is there (retainWhileLoading), rendered at its own size × the screen's ratio.
// In a dark theme a picture (not a drawing: it has a dark version) lies on a light backing under
// its painted area (AppSettings.lightPictureBacking, on by default — or the note's own
// `picture-backing` option, PageController.pictureBacking): a transparent PNG or SVG with
// dark lines would vanish into the dark page; an opaque photo covers the backing.
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
// An Obsidian base (`base`: `![[Books.base]]`, `![[Books.base#Reading]]`; docs/SYNTAX.md §10
// "Obsidian Bases") is its view, live: a DatabaseView over the .base file (`this` the note),
// edited as on the base's own page; its toasts go out as viewToast().
// A sound or a video (`media`: `![[Nagranie.m4a]]`, `![](film.mp4#t=83)`; docs/UI.md "Audio and
// video") is a player (MediaEmbedTile): its "Transcribe" emits transcribeRequested(path, target),
// its "Copy link to …" copyRequested(text).
Column {
    id: root

    property list<var> embeds
    /// The page's controller and editor (an embedded base's view: rendering, edits, the keys).
    property PageController controller: null
    property BlockEditor editor: null
    /// Reading mode (BlockEditor.readOnly): a click beside a picture edits nothing (no text cursor).
    property bool readOnly: false
    /// Pictures get their hairline frame: the note's own option (`picture-frames`, docs/SYNTAX.md
    /// §4 "Note options"), else the device's setting.
    readonly property int notePictureFrames: root.controller !== null ? root.controller.pictureFrames : -1
    readonly property bool pictureFrames: notePictureFrames >= 0 ? notePictureFrames > 0 : AppSettings.pictureFrames
    /// In a dark theme pictures lie on a light backing: the note's own option (`picture-backing`),
    /// else the device's setting.
    readonly property int notePictureBacking: root.controller !== null ? root.controller.pictureBacking : -1
    readonly property bool pictureBacking: Theme.dark && (notePictureBacking >= 0 ? notePictureBacking > 0
                                                                                  : AppSettings.lightPictureBacking)
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
    /// An embedded base's toast (DatabaseView.toastRequested; `undo`: model.undoLast() puts it back).
    signal viewToast(string text, string kind, bool undo, ViewResultModel model)
    /// A finger held beside the picture / chip (touch UIs).
    signal backgroundLongPressed()
    /// A recording's "Transcribe" (MediaEmbedTile): the editor queues it (AppController.speech).
    signal transcribeRequested(string path, string target)
    /// A recording's "Copy link to …": `text` to the clipboard.
    signal copyRequested(string text)

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

    readonly property url baseSource: Qt.resolvedUrl("DatabaseView.qml")
    readonly property url pdfSource: Qt.resolvedUrl("PdfEmbedTile.qml")
    readonly property url mediaSource: Qt.resolvedUrl("../media/MediaEmbedTile.qml")

    component MutedText: Text {
        textFormat: Text.PlainText
        font.family: Theme.fontUi
        font.pixelSize: Theme.textSm
        color: Theme.fgMuted
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
            /// A sound or a video (BlockListModel's media embed): a player.
            readonly property bool media: rec.map.media === true
            readonly property string urlText: "" + (rec.map.url || "")
            readonly property string darkUrlText: "" + (rec.map.darkUrl || "")
            readonly property bool image: !pdf && !media && rec.map.image === true && (web || urlText !== "")
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
            /// An Obsidian base: its view (its chip without a controller).
            readonly property bool base: rec.map.base === true && root.controller !== null
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
            height: embed.base ? baseLoader.height : embed.media ? mediaLoader.height : embed.pdfTile ? pdfLoader.height : image ? picture.height : chip.height

            // The views of a base, a PDF's thumbnail and a player are loaded from their files for the
            // embeds that have them (docs/PERFORMANCE.md "QML files loaded when first used").
            Loader {
                id: baseLoader
                active: embed.base
                width: embed.width
                height: baseLoader.implicitHeight
                // (Bindings of its own, made only for a base.)
                sourceComponent: Item {
                    implicitHeight: baseView.implicitHeight
                    Loader {
                        id: baseView
                        width: parent.width
                        height: baseView.implicitHeight
                        Component.onCompleted: {
                            embed.loadValues = { controller: root.controller, editor: root.editor, language: "base",
                                                 basePath: "" + (embed.rec.map.path || ""), viewName: "" + (embed.rec.map.view || ""),
                                                 thisPath: root.controller !== null ? root.controller.path : "" }
                            baseView.setSource(root.baseSource, embed.loadValues)
                        }
                    }
                    Binding { target: baseView.item; property: "controller"; value: root.controller; restoreMode: Binding.RestoreNone }
                    Binding { target: baseView.item; property: "editor"; value: root.editor; restoreMode: Binding.RestoreNone }
                    Binding {
                        target: baseView.item
                        property: "thisPath"
                        value: root.controller !== null ? root.controller.path : ""
                        restoreMode: Binding.RestoreNone
                    }
                    Connections {
                        target: baseView.item
                        function onToastRequested(text: string, kind: string, undo: bool): void {
                            const results = QmlCall.object(baseView.item, "results") as ViewResultModel
                            root.viewToast(text, kind, undo, results)
                        }
                    }
                }
            }
            /// The next setSource()'s first values (read from a property, as compiled QML wants).
            property var loadValues: ({})

            // Beside the picture / chip: a click there edits the block (an embed-only paragraph of
            // a document has no text line to click).
            Item {
                x: embed.base ? embed.width : embed.media ? mediaLoader.width : embed.pdfTile ? pdfLoader.width : embed.image ? picture.width : chip.width
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
                /// What the loaded picture measured: the frame of a picture without a size in
                /// its header, or of another shape than its header's, keeps it when the picture
                /// lets go of its pixels out of the view's reach — its row must not change its
                /// height while the page only scrolls. A raster picture's is its decoded size, of
                /// the file's shape; such a frame is as large as its natural size (the 2048-px box
                /// below), so the two agree. `seenOf`: the file it was measured of (`seenKey`: its
                /// URL and what its header said then) — another file's measure is none.
                property size seenSize: Qt.size(0, 0)
                property string seenOf: ""
                readonly property string seenKey: embed.urlText + "|" + embed.knownWidth + "x" + embed.knownHeight
                readonly property bool seen: seenOf !== "" && seenOf === seenKey
                function noteSeen(): void {
                    if (img.status === Image.Ready && img.implicitWidth > 0 && img.implicitHeight > 0) {
                        seenSize = Qt.size(img.implicitWidth, img.implicitHeight)
                        seenOf = seenKey
                    }
                }
                /// The loaded picture has another shape than the file's header said (Qt's pixmap
                /// cache still had the file's old picture, replaced since; a photo turned by its
                /// EXIF orientation is not: Qt draws the file's own pixels): the frame takes the
                /// picture's shape, never stretching it. Normally the two agree, so nothing moves
                /// when it has loaded. A pixel off is the same shape: Qt rounds the decoded sides
                /// (an SVG's rendered ones too), and a thin picture decoded for its frame — 768 ×
                /// 15 of a banner of 2000 × 40 — would otherwise count as another one, be decoded
                /// at its natural size, agree again, and so on for good.
                readonly property bool otherShape: {
                    if (!seen || !embed.sizeKnown)
                        return false
                    const h = seenSize.width * embed.knownHeight / embed.knownWidth
                    const w = seenSize.height * embed.knownWidth / embed.knownHeight
                    return Math.abs(seenSize.height - h) > Math.max(1.5, 0.02 * h)
                        && Math.abs(seenSize.width - w) > Math.max(1.5, 0.02 * w)
                }
                /// The frame follows the picture's own measure, not the header's: no size in the
                /// header, or another shape. (Then the picture is decoded at its natural size, not
                /// at the frame's — the frame would follow what it asked for.)
                readonly property bool measured: !embed.sizeKnown || otherShape
                readonly property real naturalWidth: !measured ? embed.knownWidth : seen ? seenSize.width : 320
                readonly property real naturalHeight: !measured ? embed.knownHeight : seen ? seenSize.height : 180
                // Natural size, at most the column width and 420 px high; a picture from the
                // internet not shown yet at least as high as its placeholder.
                width: Math.min(root.width, naturalWidth, 420 * naturalWidth / Math.max(1, naturalHeight))
                height: Math.max(Math.round(width * naturalHeight / Math.max(1, naturalWidth)),
                                 embed.webPending ? Math.ceil(placeholder.implicitHeight) + 2 * Theme.space3 : 0)
                radius: Theme.radius
                // The page's colour: a picture's transparent parts show the page, as beside it.
                // The hairline frame only with pictureFrames (the note's or the device's), or while there is no
                // picture to show (loading, waiting for "Show", a format the device cannot decode):
                // then the frame marks its place.
                color: Theme.bg
                border.width: root.pictureFrames || !loaded ? 1 : 0
                border.color: Theme.borderSubtle
                clip: true

                // Only in or near the window's view (NearView: the frame's place, whatever
                // scrolls it), and decoded for the frame, not for the file: a phone photo (12 MP)
                // shown 560 × 420 took 12.6 MB at the 2048 px it was decoded at before, and a
                // texture of 16.8 MB with its mipmaps — every picture of a page at once, as a page
                // is created whole. The side: the frame's longer one in device pixels, in steps
                // (PictureDecode), at most 2048; a frame that follows its picture's own measure
                // (`measured`) gets the 2048 box, never its own size back. A square box: Qt fits
                // the file into it with its aspect ratio and scales nothing up. No
                // PreserveAspectFit: with it Qt also scales small images *up* to sourceSize; the
                // frame has the picture's aspect ratio anyway. SVG is rendered at sourceSize
                // itself — both sides given, it would be stretched to a 2048 px square — so it is
                // fitted (aspect kept) into the largest box shown.
                // (A picture without a size in its header is measured first, as soon as it
                // exists — were it left until it came near, its row would grow under the reader
                // scrolling to it.)
                NearView.enabled: embed.image && (embed.sizeKnown || picture.seen)
                readonly property bool near: NearView.near
                readonly property int decodeSide: embed.scalable || measured ? PictureDecode.maxSide
                    : PictureDecode.side(width, height, Screen.devicePixelRatio)
                /// The URL the picture is loaded from once its frame is near the view ("": none).
                readonly property string wantedUrl: !embed.image || !embed.decodable || embed.webPending ? ""
                    : embed.drawing && Theme.dark && embed.darkUrlText !== "" ? embed.darkUrlText
                    : embed.urlText
                /// What the Image loads: `wantedUrl` near the view, "" out of its reach. Handed over
                /// by hand (hold()) for the order of two things: the picture shown stops being
                /// kept *before* another file's URL is set — kept, it would be drawn in the new
                /// picture's frame until that one is there. (Image.retainWhileLoading switched off
                /// after the URL was set would drop the picture being loaded instead.)
                property string heldUrl: ""
                /// The picture shown is `heldUrl`'s: decoded again for a frame of another size, it
                /// stays until the new one is there.
                property bool heldShown: false
                function hold(): void {
                    const url = picture.near ? picture.wantedUrl : ""
                    if (url === picture.heldUrl)
                        return
                    picture.heldShown = false
                    picture.heldUrl = url
                }
                onWantedUrlChanged: picture.hold()
                onNearChanged: picture.hold()
                Component.onCompleted: picture.hold()

                // The light backing in a dark theme (under the painted picture only: an SVG fitted
                // into its frame leaves the rest on the page's colour).
                Rectangle {
                    objectName: "embedPictureBacking"
                    visible: picture.loaded && !embed.drawing && root.pictureBacking
                    x: img.x + (img.width - img.paintedWidth) / 2
                    y: img.y + (img.height - img.paintedHeight) / 2
                    width: img.paintedWidth
                    height: img.paintedHeight
                    radius: Math.max(0, Theme.radius - 1)
                    color: Theme.pictureBacking
                }
                Image {
                    id: img
                    objectName: "embedImage"
                    anchors.fill: parent
                    anchors.margins: 1
                    autoTransform: true // a photo stored sideways, upright by its EXIF orientation
                    source: picture.heldUrl
                    // (Logical pixels for a drawing: Qt renders image providers at the screen's ratio.)
                    sourceSize: embed.drawing && embed.sizeKnown
                                ? Qt.size(Math.ceil(embed.knownWidth), Math.ceil(embed.knownHeight))
                                : embed.scalable ? Qt.size(1024, 420) : Qt.size(picture.decodeSide, picture.decodeSide)
                    // The picture shown stays until the next one is there: a drawing's other theme
                    // or version, a picture decoded again for a wider frame — not another file's.
                    retainWhileLoading: embed.drawing || picture.heldShown
                    fillMode: embed.scalable ? Image.PreserveAspectFit : Image.Stretch
                    asynchronous: true
                    smooth: true
                    // Mipmaps (a third more texture) only where the picture is drawn much smaller
                    // than it was decoded: an SVG rendered into its largest box, a picture at its
                    // natural size in a narrow frame. One decoded for its frame needs none.
                    mipmap: embed.scalable || picture.measured
                    onStatusChanged: {
                        picture.noteSeen()
                        if (img.status === Image.Ready)
                            picture.heldShown = true
                    }
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
                    MutedText {
                        objectName: "webPictureText"
                        visible: text !== ""
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        maximumLineCount: placeholder.failed ? 3 : 2
                        elide: Text.ElideRight
                        text: placeholder.failed ? embed.rec.map.error || "" : embed.alt
                    }
                    // The site: what "Show" would contact, never elided away by a long alt text.
                    MutedText {
                        objectName: "webPictureHost"
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideMiddle
                        text: qsTr("Picture from %1").arg(embed.host)
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
                MutedText {
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
                onActiveChanged: embed.loadPdf()
                Component.onCompleted: embed.loadPdf()
            }
            function loadPdf(): void {
                if (!pdfLoader.active || pdfLoader.item !== null)
                    return
                embed.loadValues = { entry: embed.modelData, columnWidth: root.width }
                pdfLoader.setSource(root.pdfSource, embed.loadValues)
                QmlCall.connect(pdfLoader.item, "clicked", root, "pdfClicked")
                // (Its column's width followed without an object of its own.)
                QmlCall.connect(root, "widthChanged", embed, "followWidth")
            }
            function followWidth(): void {
                if (pdfLoader.item !== null)
                    QmlCall.set(pdfLoader.item, "columnWidth", root.width)
                if (mediaLoader.item !== null)
                    QmlCall.set(mediaLoader.item, "columnWidth", root.width)
            }

            // A sound or a video: its player.
            Loader {
                id: mediaLoader
                active: embed.media
                visible: active
                onActiveChanged: embed.loadMedia()
                Component.onCompleted: embed.loadMedia()
            }
            function loadMedia(): void {
                if (!mediaLoader.active || mediaLoader.item !== null)
                    return
                embed.loadValues = { entry: embed.modelData, columnWidth: root.width }
                mediaLoader.setSource(root.mediaSource, embed.loadValues)
                QmlCall.connect(mediaLoader.item, "transcribeRequested", root, "transcribeRequested")
                QmlCall.connect(mediaLoader.item, "copyRequested", root, "copyRequested")
                QmlCall.connect(root, "widthChanged", embed, "followWidth")
            }

            BChip {
                id: chip
                objectName: embed.pdf ? "pdfEmbedChip" : ""
                visible: !embed.image && !embed.pdfTile && !embed.base && !embed.media
                interactive: true
                iconName: embed.pdf ? "file-text" : embed.rec.map.image === true ? "image" : "file"
                text: embed.name
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
