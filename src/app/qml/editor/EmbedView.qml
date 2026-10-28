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
// A drawing (the embed's `drawing`: an SVG with an embedded scene, docs/SYNTAX.md §2) is shown at
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
// names the site with "Pokaż" (asking first is the default, AppSettings.webPicturesShown) and
// "Zapisz w notatniku", a spinner while it loads, the reason when it failed; a click on the shown
// picture opens it on its own (webOpen: WebPictureViewer, like the picture view of an
// attachment), a right-click or a long press its menu (webMenu: WebPictureMenu). Its frame grows
// to fit what it says (a phone's narrow column, a long alt text), and keeps that height while the
// picture loads, so the row changes once: when the picture is there.
Column {
    id: root

    property var embeds: []
    signal clicked(string target)
    /// A click on a drawing (`drawing`: an SVG with an embedded scene): the drawing editor.
    signal drawingClicked(string path)
    /// A picture from the internet: "Pokaż" / "Spróbuj ponownie", "Zapisz w notatniku", its menu.
    signal showWeb(string url)
    signal downloadWeb(string url)
    signal webMenu(string url, string host, Item item, real x, real y)
    /// A click on a shown picture from the internet: `picture` is its embed (the cached copy).
    signal webOpen(var picture)
    signal backgroundClicked()
    /// A finger held beside the picture / chip (touch UIs).
    signal backgroundLongPressed()

    /// See BlockEditor.autoTextSafe (BChip shows its text with Text.AutoText).
    function autoTextSafe(s) {
        return s && s.indexOf("<") >= 0 ? TextHelper.escapeHtml(s) : (s || "")
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
            readonly property bool web: modelData.web === true
            readonly property bool image: modelData.image && (web || modelData.url.toString() !== "")
            /// A picture from the internet not shown (yet): the frame tells why and what to do.
            readonly property bool webPending: web && modelData.state !== "ready"
            readonly property bool decodable: modelData.decodable !== false
            readonly property bool drawing: modelData.drawing === true
            /// SVG: a local file by its name; a picture from the internet by the format the host
            /// found in its bytes (its URL need not end with .svg, `name` is "host › segment").
            readonly property bool scalable: web ? /^svgz?$/i.test(modelData.format || "")
                                                 : /\.svgz?$/i.test(modelData.name || "")
            /// The picture's size as the Image below shows it once loaded: a raster picture's pixels,
            /// at most 2048 a side; an SVG fitted into 1024 × 420 (rendered at the screen's pixel
            /// ratio) — null when the model does not know the file's size (then: once loaded).
            readonly property var knownSize: {
                const w = modelData.width || 0
                const h = modelData.height || 0
                if (w <= 0 || h <= 0)
                    return null
                if (modelData.drawing === true) {
                    // A drawing at its own size (one unit a pixel, as drawn), never enlarged.
                    const s = Math.min(1, 1024 / w, 420 / h)
                    return { width: Math.floor(w * s), height: Math.floor(h * s) }
                }
                const dpr = scalable && Screen.devicePixelRatio > 0 ? Screen.devicePixelRatio : 1
                const box = scalable ? [1024 * dpr, 420 * dpr] : [2048, 2048]
                const k = scalable || w > box[0] || h > box[1] ? Math.min(box[0] / w, box[1] / h) : 1
                return { width: Math.floor(w * k) / dpr, height: Math.floor(h * k) / dpr }
            }
            width: root.width
            height: image ? picture.height : chip.height

            // Beside the picture / chip: a click there edits the block (an embed-only paragraph of
            // a document has no text line to click).
            Item {
                x: embed.image ? picture.width : chip.width
                width: Math.max(0, embed.width - x)
                height: embed.height
                HoverHandler { cursorShape: Qt.IBeamCursor; enabled: !Theme.touchUi }
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
                readonly property bool loaded: img.status === Image.Ready && img.implicitWidth > 0 && img.implicitHeight > 0
                /// The loaded picture has another shape than the file's header said (Qt's pixmap
                /// cache still had the file's old picture, replaced since): the frame takes the
                /// picture's shape, never stretching it. Normally the two agree, so nothing moves
                /// when it has loaded (an SVG's rendered size differs slightly, in its rounding).
                readonly property bool otherShape: loaded && embed.knownSize !== null
                    && Math.abs(img.implicitWidth * embed.knownSize.height - img.implicitHeight * embed.knownSize.width)
                       > 0.02 * img.implicitWidth * embed.knownSize.height
                readonly property real naturalWidth: embed.knownSize && !otherShape ? embed.knownSize.width
                                                     : loaded ? img.implicitWidth : 320
                readonly property real naturalHeight: embed.knownSize && !otherShape ? embed.knownSize.height
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
                            : embed.drawing && Theme.dark && String(embed.modelData.darkUrl || "") !== "" ? embed.modelData.darkUrl
                            : embed.modelData.url
                    // (Logical pixels for a drawing: Qt renders image providers at the screen's ratio.)
                    sourceSize: embed.drawing && embed.knownSize
                                ? Qt.size(Math.ceil(embed.knownSize.width), Math.ceil(embed.knownSize.height))
                                : embed.scalable ? Qt.size(1024, 420) : Qt.size(2048, 2048)
                    retainWhileLoading: embed.drawing
                    fillMode: embed.scalable ? Image.PreserveAspectFit : Image.Stretch
                    asynchronous: true
                    smooth: true
                    mipmap: true
                    Accessible.role: Accessible.Graphic
                    Accessible.name: embed.modelData.alt !== "" ? embed.modelData.alt : embed.modelData.name
                }
                BSpinner {
                    anchors.centerIn: parent
                    size: 20
                    running: img.status === Image.Loading || (embed.web && embed.modelData.state === "loading")
                    visible: running
                }
                // A picture from the internet waiting to be shown (asking first) or that failed.
                // While it loads only the spinner shows, but the placeholder keeps its place (a
                // hidden Column would measure 0: the frame's height follows it).
                Column {
                    id: placeholder
                    objectName: "webPicturePlaceholder"
                    readonly property bool failed: embed.modelData.state === "failed"
                    visible: embed.webPending
                    opacity: embed.modelData.state === "loading" ? 0 : 1
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
                        text: placeholder.failed ? embed.modelData.error || "" : embed.modelData.alt || ""
                        textFormat: Text.PlainText
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.textSm
                        color: Theme.fgMuted
                    }
                    // The site: what "Pokaż" would contact, never elided away by a long alt text.
                    Text {
                        objectName: "webPictureHost"
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideMiddle
                        text: qsTr("Picture from %1").arg(embed.modelData.host)
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
                            onClicked: root.showWeb(embed.modelData.target)
                        }
                        BButton {
                            id: saveButton
                            objectName: "webPictureSave"
                            size: "small"
                            variant: "ghost"
                            iconName: "download"
                            text: qsTr("Save to notebook")
                            onClicked: root.downloadWeb(embed.modelData.target)
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
                          ? embed.modelData.name + "\n"
                            + qsTranslate("ContentPictureView", "Pictures in the %1 format cannot be shown on this device.")
                              .arg(embed.modelData.format)
                          : qsTr("Cannot load %1").arg(embed.modelData.name)
                    textFormat: Text.PlainText
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.textSm
                    color: Theme.fgMuted
                }
                TapHandler {
                    enabled: !embed.webPending
                    acceptedButtons: Qt.LeftButton
                    onTapped: {
                        if (embed.web)
                            root.webOpen(embed.modelData)
                        else if (embed.modelData.drawing === true)
                            root.drawingClicked(embed.modelData.path)
                        else
                            root.clicked(embed.modelData.target)
                    }
                    onLongPressed: if (embed.web) root.webMenu(embed.modelData.target, embed.modelData.host, picture,
                                                                point.position.x, point.position.y)
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
                    onTapped: (point) => root.webMenu(embed.modelData.target, embed.modelData.host, picture,
                                                      point.position.x, point.position.y)
                }
                HoverHandler { cursorShape: Qt.PointingHandCursor; enabled: !Theme.touchUi && !embed.webPending }
                // A drawing: a pen in the corner says a click edits it.
                Rectangle {
                    objectName: "embedDrawingBadge"
                    visible: embed.modelData.drawing === true && picture.loaded && picture.width >= 72 && picture.height >= 48
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

            BChip {
                id: chip
                visible: !embed.image
                interactive: true
                iconName: embed.modelData.image ? "image" : "file"
                text: root.autoTextSafe(embed.modelData.name)
                onClicked: root.clicked(embed.modelData.target)
            }
        }
    }
}
