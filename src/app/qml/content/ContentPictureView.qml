pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// ContentPictureView — a picture attachment opened as a page (assets/photo.png): the picture
// itself, fitted into the view with its aspect ratio kept. Fitting only shrinks a raster picture
// (a smaller one keeps its own pixels, never blurred bigger); an SVG is drawn to fill the view.
// When the fitted picture differs from its own pixels, a click on it — or the size button in the
// bar — shows those pixels, where it can be dragged around; the button switches back. Under the
// fitted picture, like a page's references (the picture then sits at the top, not centred): the
// notes that embed or link it (ContentBacklinks).
// The bar holds the file's name (plain text: a name from anywhere is no markup), the size toggle
// and "Show as text" ⇄ "Show picture": the file read as text, read-only — a binary picture's
// bytes are mojibake there, and a typed character would have been saved over the picture; the
// view is for text-like assets such as SVG (`asText`, PageView switches it, Ctrl+E too).
// In a narrow view (a phone; on the desktop between the sidebar and the side panel) the size
// toggle is an icon, its text in the tooltip and for screen readers (as text it left the file's
// name no room).
// A picture whose format this device cannot decode (WebP or AVIF without Qt's image plugins:
// Workspace.canDecodePicture) is named so instead of a load error.
// A drawing (Workspace.isDrawing: an SVG with a scene, a `.excalidraw` scene, an Obsidian
// Excalidraw plugin file opened as a page — PageView.drawingPage) has "Edit drawing" in the bar
// (the drawing editor, route "drawing"); the picture follows the file when it is saved again, and
// the app's theme (light or dark paper, as the drawing chooses: Workspace.drawingUrl, the drawing
// image provider).
// `textOnly` (PageView's safety net for attachments that are no note, no picture and no PDF —
// DOCX, ZIP, MP3…): the bar names the file and says "Read-only", the view is the file read
// as text, nothing else — no file's bytes ever reach the block editor; "Open in another app"
// for a kind on the allow-list (Workspace.canOpenInOtherApp: DOCX, XLSX, ODT, MP3… by name and
// first bytes; never a program, a script, a page or an archive).
// The text view reads at most `textLimit` bytes of the file (Workspace.readTextStart: a photo is
// never read whole for it), again whenever the file changes (sync, another program).
// Text OCR read in the picture (the whole-notebook search, docs/UI.md "Searching attachments";
// Workspace.attachments): opened from a search result (route.search) the places of its words are
// framed on the picture, "2 places" in the bar with × to hide them; "Recognized text" shows what OCR
// read, read-only and selectable (copied from a whiteboard's photo).
//
//   ContentPictureView { path: routePath; asText: pictureAsText
//                        rendererPalette: editor.rendererPalette
//                        onTextToggleRequested: pictureAsText = !pictureAsText }
//
// The URL comes from Workspace.assetUrl (file:// on desktop/Android, data:/blob on WebAssembly),
// the pixel size from Workspace.pictureSize (the file's header, no decode): the picture's own
// size in the pixels view, and the decode box — both of its sides follow the picture's shape, so
// Qt scales it without stretching it to a box of another shape (an SVG is rendered at sourceSize
// itself). A raster picture is decoded for what the view shows: fitted, at the fitted picture's
// size in device pixels (PictureDecode: in steps, so that a window being resized decodes it again
// only now and then — a 12 MP photo fitted into a 1280 × 800 window holds 3 MB, not the 12.6 MB of
// 2048 px a side, with a texture as large and no mipmaps); at its own pixels at most 2048 px a
// side (a 12 MP photo would otherwise take 48 MB) — and never more than its own pixels (no
// PreserveAspectFit, which makes Qt scale a smaller picture *up* to sourceSize). The picture shown
// stays while the other size is decoded. An SVG is re-rendered up to 2048 px a side (vector: a
// bigger render stays sharp — a raster picture would only get a bigger item over the same pixels).
Item {
    id: root

    objectName: "pictureView" // (the shell's self-test)

    property string path
    /// The route (PageView's): `search` — the words a search result found here.
    property var route: ({})
    /// The file read as text (read-only) instead of the picture.
    property bool asText: false
    /// Not a picture at all (PageView.fileRoute): only its text, read-only (asText with it).
    property bool textOnly: false
    /// Renderer colours for the references (BlockEditor.rendererPalette).
    property var rendererPalette: ({})
    /// The bar's "Show as text" / "Show picture" (PageView switches `asText`).
    signal textToggleRequested()

    /// Fitted into the view ↔ the picture's own pixels (the flickable then).
    property bool actualSize: false
    /// At most this many bytes of the file are read and shown as text (a photo's bytes would
    /// take seconds to lay out, for nothing anyone can read).
    property int textLimit: 200 * 1024

    readonly property string fileName: {
        const i = Js.lastIndexOf(root.path, "/")
        return i < 0 ? root.path : Js.sliceFrom(root.path, i + 1)
    }
    /// Drawn to fill the view: an SVG, a drawing (its preview, drawn at any size).
    readonly property bool scalable: !root.textOnly && (root.drawing || Js.test(root.fileName, "\\.svg$", "i"))
    /// This device can decode the picture's format (see above); a drawing's preview always.
    readonly property bool decodable: !root.textOnly && (root.drawing || (root.path !== "" && Workspace.canDecodePicture(root.path)))
    /// "WEBP" for assets/meadow.webp (the message for a format this device cannot show).
    readonly property string formatName: {
        const i = Js.lastIndexOf(root.fileName, ".")
        return i < 0 ? "" : Js.toUpperCase(Js.sliceFrom(root.fileName, i + 1))
    }
    /// Grows when the file changes on disk (a drawing saved again): its URL is asked for again
    /// (a drawing's carries its version, Workspace.assetUrl).
    property int fileRevision: 0
    readonly property url source: !root.decodable || root.fileRevision < 0 ? ""
                                  : root.drawing ? Workspace.drawingUrl(root.path, Theme.dark)
                                  : Workspace.assetUrl(root.path)
    /// A drawing (docs/SYNTAX.md §2 "Drawings": an SVG with a scene, a `.excalidraw` scene, an
    /// Obsidian Excalidraw plugin file): the bar offers the drawing editor.
    readonly property bool drawing: !root.textOnly && root.path !== "" && root.fileRevision >= 0 && Workspace.isDrawing(root.path)

    Connections {
        target: Workspace
        function onFileChanged(path: string) {
            if (path === root.path)
                root.fileRevision++
        }
    }
    /// The picture's pixels from its header (≤ 0: not in the header — the loaded picture's then);
    /// a drawing's preview size (again when the file changes). Set by readHeaderSize() (a call's
    /// size cannot be bound compiled), whenever the path, the file or textOnly change.
    property size headerSize: Qt.size(0, 0)
    function readHeaderSize(): void {
        if (root.textOnly)
            root.headerSize = Qt.size(0, 0)
        else
            root.headerSize = Workspace.pictureSize(root.path)
    }
    onFileRevisionChanged: root.readHeaderSize()
    onTextOnlyChanged: root.readHeaderSize()
    Component.onCompleted: {
        root.readHeaderSize()
        root.holdSource()
    }
    /// What the picture is loaded from: `source`, handed over by hand (holdSource()) for the
    /// order of two things: the picture shown stops being kept *before* another file's URL is
    /// set — kept, it would be drawn in the new picture's frame until that one is there.
    /// (Image.retainWhileLoading switched off after the URL was set would drop the picture being
    /// loaded instead.)
    property string heldSource: ""
    /// The picture shown is `heldSource`'s: decoded again for another size (fitted ↔ its own
    /// pixels, a window resized), it stays until the new one is there.
    property bool heldShown: false
    function holdSource(): void {
        const url = "" + root.source
        if (url === root.heldSource)
            return
        root.heldShown = false
        root.heldSource = url
    }
    onSourceChanged: root.holdSource()
    readonly property bool headerKnown: headerSize.width > 0 && headerSize.height > 0
    /// The loaded picture's size (0 × 0: none, or one being loaded without the last one kept):
    /// what `shownSize` is without a header. Kept by hand from the picture's status, not bound to
    /// its implicit size: once a load has begun Qt resets that inside the next change of the
    /// picture's geometry (QQuickImage::updatePaintedGeometry) — in the middle of the bindings
    /// that follow this very size, a binding loop.
    property size loadedSize: Qt.size(0, 0)
    function noteLoaded(): void {
        if (picture.status === Image.Ready)
            root.loadedSize = Qt.size(picture.implicitWidth, picture.implicitHeight)
        else if (!(picture.status === Image.Loading && picture.retainWhileLoading))
            root.loadedSize = Qt.size(0, 0)
    }
    /// The picture's own pixels in the pixels view (the loaded size when the header has none).
    readonly property size shownSize: headerKnown ? headerSize
                                     : Qt.size(Math.max(1, loadedSize.width), Math.max(1, loadedSize.height))
    /// What the picture is decoded at: its own shape; a raster picture fitted into the view at
    /// what the view shows of it, its own pixels at most 2048 px a side (see above).
    /// (-1 × -1: none — Image.sourceSize reset.)
    readonly property size decodeSize: {
        if (!root.headerKnown)
            return root.scalable ? Qt.size(-1, -1) : Qt.size(PictureDecode.maxSide, PictureDecode.maxSide)
        const longer = Math.max(root.headerSize.width, root.headerSize.height)
        // The fitted picture by the header and the view themselves, not through shownSize and
        // fittedSize: those follow the loaded picture when a header has no size, and what
        // the picture is asked for must not hang on what loading it changes.
        const fit = Math.min(1, flick.width / root.headerSize.width, flick.height / root.headerSize.height)
        const side = root.scalable ? PictureDecode.maxSide
                   : root.actualSize ? Math.min(longer, PictureDecode.maxSide)
                   : PictureDecode.sideWithin(root.headerSize.width * fit, root.headerSize.height * fit,
                                              Screen.devicePixelRatio, Math.min(longer, PictureDecode.maxSide))
        const k = side / longer
        return Qt.size(Math.max(1, Math.round(root.headerSize.width * k)),
                       Math.max(1, Math.round(root.headerSize.height * k)))
    }
    /// The picture fitted into the view (logical pixels).
    readonly property size fittedSize: Qt.size(Math.max(1, Math.round(root.shownSize.width * root.fitScale)),
                                               Math.max(1, Math.round(root.shownSize.height * root.fitScale)))
    /// The words the search found here (route.search) and their places on the picture (fractions of
    /// its own pixels, Workspace.attachments.matchRects: OCR's words), again when the file changes or
    /// OCR reads more; × in the bar hides them.
    readonly property string searchText: AppController.routeText(root.route, "search")
    property bool matchesHidden: false
    onSearchTextChanged: root.matchesHidden = false
    readonly property list<var> matches: {
        // (Both used for real: qmlcachegen compiles a bare `void x` away, and the dependency with it.)
        const revision = root.fileRevision + Workspace.attachments.indexedPages
        return revision >= 0 && root.searchText !== "" && !root.textOnly && !root.matchesHidden
               ? Workspace.attachments.matchRects(root.path, 1, root.searchText) : []
    }
    /// What OCR read in the picture ("" — not read, OCR off, no words).
    readonly property string recognized: {
        const revision = root.fileRevision + Workspace.attachments.indexedPages
        return revision >= 0 && !root.textOnly && root.path !== "" ? Workspace.attachments.recognizedText(root.path, 1) : ""
    }
    /// The recognized text shown instead of the picture.
    property bool showRecognized: false
    /// The fitted picture's scale: at most 1 for a raster picture (fitting only shrinks), any
    /// for an SVG.
    readonly property real fitScale: {
        const k = Math.min(flick.width / root.shownSize.width, flick.height / root.shownSize.height)
        return root.scalable ? k : Math.min(1, k)
    }
    /// The picture is loaded and drawn (the shell's self-test) — also while it is decoded again at
    /// another size, the one shown kept meanwhile.
    readonly property bool pictureLoaded: (picture.status === Image.Ready
                                           || (picture.status === Image.Loading && picture.retainWhileLoading
                                               && picture.implicitWidth > 0))
                                          && picture.paintedWidth > 0
    /// The fitted picture is not its own pixels: the size toggle is offered.
    readonly property bool canZoom: root.pictureLoaded && Math.abs(root.fitScale - 1) > 0.01
    readonly property string sizeToggleText: root.actualSize ? qsTr("Fit to view") : qsTr("Zoom to actual size")
    /// The size toggle is an icon (see above): the view is narrow — its own width, not the
    /// window's.
    readonly property bool iconSizeToggle: root.width < Theme.compactBreakpoint
    /// The toggle's icon: which way the picture's size changes.
    readonly property string sizeToggleIcon: (root.fitScale < 1) !== root.actualSize ? "zoom-in" : "zoom-out"

    // Another picture: start fitted from the top left (the flickable clamps itself to the
    // smaller content, but contentX/Y are plain state without a binding to reset them).
    onPathChanged: {
        root.readHeaderSize()
        root.actualSize = false
        root.showRecognized = false
        flick.contentX = 0
        flick.contentY = 0
    }

    /// Fitted ↔ the picture's own pixels, centred in the other view.
    function toggleSize(): void {
        if (!root.canZoom && !root.actualSize)
            return
        root.actualSize = !root.actualSize
        flick.contentX = Math.max(0, (flick.contentWidth - flick.width) / 2)
        flick.contentY = root.actualSize ? Math.max(0, (flick.contentHeight - flick.height) / 2) : 0
    }

    // The bar: the file's name and the view's controls.
    Rectangle {
        id: bar

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        implicitHeight: barRow.implicitHeight + Theme.space3 * 2
        color: Theme.bg

        Rectangle {
            // A hairline under the bar.
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.borderSubtle
        }

        RowLayout {
            id: barRow

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.margins: Theme.space4
            spacing: Theme.space3

            BIcon {
                name: root.textOnly ? "file" : root.drawing ? "pen-tool" : "image"
                size: Theme.iconSize
                color: Theme.fgMuted
                Layout.alignment: Qt.AlignVCenter
            }
            Text {
                objectName: "pictureName" // (the shell's self-test)
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.alignment: Qt.AlignVCenter
                text: root.fileName
                textFormat: Text.PlainText // a file name from anywhere (sync, imports) is no markup
                elide: Text.ElideMiddle
                font.family: Theme.fontUi
                font.pixelSize: Theme.textSm
                color: Theme.fgMuted
            }
            // The size toggle: with its text where there is room, an icon in a narrow view.
            BButton {
                objectName: "pictureSizeButton"
                visible: !root.iconSizeToggle && !root.asText && (root.canZoom || root.actualSize)
                size: "small"
                variant: "ghost"
                text: root.sizeToggleText
                Layout.alignment: Qt.AlignVCenter
                onClicked: root.toggleSize()
            }
            BIconButton {
                objectName: "pictureSizeButton"
                visible: root.iconSizeToggle && !root.asText && (root.canZoom || root.actualSize)
                size: "small"
                iconName: root.sizeToggleIcon
                label: root.sizeToggleText
                Layout.alignment: Qt.AlignVCenter
                onClicked: root.toggleSize()
            }
            // The search's words on the picture: how many places, × hides them.
            BText {
                objectName: "pictureMatches"
                // (A narrow bar keeps its room for the buttons: the frames say it.)
                visible: root.matches.length > 0 && !root.asText && !root.showRecognized && !root.iconSizeToggle
                text: qsTr("%n place(s)", "", root.matches.length)
                variant: "caption"
                color: Theme.accentText
                Layout.alignment: Qt.AlignVCenter
            }
            BIconButton {
                objectName: "pictureMatchesClear"
                visible: root.matches.length > 0 && !root.asText && !root.showRecognized
                size: "small"
                iconName: "x"
                label: qsTr("Hide the places found")
                Layout.alignment: Qt.AlignVCenter
                onClicked: root.matchesHidden = true
            }
            BButton {
                objectName: "pictureRecognizedButton"
                visible: root.recognized !== "" && !root.asText
                size: "small"
                variant: root.showRecognized ? "secondary" : "ghost"
                iconName: root.showRecognized ? "image" : "scan-text"
                text: root.iconSizeToggle ? "" : root.showRecognized ? qsTr("Show picture") : qsTr("Recognized text")
                Accessible.name: root.showRecognized ? qsTr("Show picture") : qsTr("Recognized text")
                Layout.alignment: Qt.AlignVCenter
                onClicked: root.showRecognized = !root.showRecognized
            }
            BButton {
                objectName: "pictureDrawingButton"
                visible: root.drawing && !root.asText
                size: "small"
                variant: "secondary"
                iconName: "pen-tool"
                text: root.iconSizeToggle ? qsTr("Edit") : qsTr("Edit drawing")
                Layout.alignment: Qt.AlignVCenter
                onClicked: AppController.navigate({ view: "drawing", path: root.path })
            }
            BText {
                objectName: "pictureReadOnly"
                visible: root.textOnly
                text: qsTr("Read-only")
                variant: "muted"
                Layout.alignment: Qt.AlignVCenter
            }
            // A document, a sheet, audio… of the allow-list (Workspace.canOpenInOtherApp: its name
            // and its first bytes): the system's app for it, a new tab in the browser.
            BButton {
                objectName: "pictureOpenElsewhere"
                visible: root.textOnly && root.fileRevision >= 0 && root.path !== ""
                         && Workspace.canOpenInOtherApp(root.path)
                size: "small"
                variant: "secondary"
                iconName: "external-link"
                text: root.iconSizeToggle ? qsTr("Open elsewhere")
                    : Qt.platform.os === "wasm" ? qsTr("Open in a new tab") : qsTr("Open in another app")
                Accessible.name: Qt.platform.os === "wasm" ? qsTr("Open in a new tab") : qsTr("Open in another app")
                Layout.alignment: Qt.AlignVCenter
                onClicked: Workspace.openInOtherApp(root.path)
            }
            BButton {
                objectName: "pictureTextButton"
                visible: !root.textOnly && !root.iconSizeToggle
                size: "small"
                variant: "ghost"
                iconName: root.asText ? "image" : "code"
                text: root.asText ? qsTr("Show picture") : qsTr("Show as text")
                Layout.alignment: Qt.AlignVCenter
                onClicked: root.textToggleRequested()
            }
            // (Narrow: an icon, its text in the tooltip.)
            BIconButton {
                objectName: "pictureTextButton"
                visible: !root.textOnly && root.iconSizeToggle
                size: "small"
                iconName: root.asText ? "image" : "code"
                label: root.asText ? qsTr("Show picture") : qsTr("Show as text")
                Layout.alignment: Qt.AlignVCenter
                onClicked: root.textToggleRequested()
            }
        }
    }

    Flickable {
        id: flick

        anchors.fill: parent
        anchors.topMargin: bar.height
        visible: !root.asText && !root.showRecognized
        clip: true
        // Fitted: the picture fits into the view, its references follow under it.
        contentWidth: root.actualSize ? Math.max(width, picture.width) : width
        contentHeight: root.actualSize ? Math.max(height, picture.height)
                     : refs.visible ? Math.max(height, refs.y + refs.height + Theme.space12) : height
        interactive: root.actualSize || refs.visible
        boundsBehavior: Flickable.StopAtBounds
        TouchFlick.enabled: !root.actualSize
        T.ScrollBar.vertical: BScrollBar {}

        Image {
            id: picture

            objectName: "pictureImage" // (the shell's self-test)
            x: Math.max(0, (flick.width - width) / 2)
            y: refs.visible ? Math.min(Theme.space4, Math.max(0, (flick.height - height) / 2))
                            : Math.max(0, (flick.height - height) / 2)
            width: root.actualSize ? root.shownSize.width : root.fittedSize.width
            height: root.actualSize ? root.shownSize.height : root.fittedSize.height
            source: root.asText ? "" : root.heldSource
            sourceSize: root.decodeSize
            // A drawing's other theme or version, the picture decoded for another size: the shown
            // one stays until it is there — not another file's (holdSource()).
            retainWhileLoading: root.drawing || root.heldShown
            onStatusChanged: {
                root.noteLoaded()
                if (picture.status === Image.Ready)
                    root.heldShown = true
            }
            fillMode: root.scalable ? Image.PreserveAspectFit : Image.Stretch
            asynchronous: true
            // (Only where the picture is drawn smaller than it was decoded or rendered.)
            mipmap: root.scalable || !root.headerKnown
            Accessible.role: Accessible.Graphic
            Accessible.name: root.fileName

            TapHandler { onTapped: root.toggleSize() }
            HoverHandler {
                cursorShape: root.canZoom || root.actualSize ? Qt.PointingHandCursor : Qt.ArrowCursor
                enabled: !Theme.touchUi
            }
        }

        // The places of the search's words (OCR's boxes on the file's own pixels): framed in the
        // accent, over the picture at any size.
        Repeater {
            model: root.matches
            delegate: Rectangle {
                required property rect box
                objectName: "pictureMatch"
                x: picture.x + box.x * picture.width - 2
                y: picture.y + box.y * picture.height - 2
                width: box.width * picture.width + 4
                height: box.height * picture.height + 4
                radius: 2
                color: Theme.accent.alpha(0.22)
                border.color: Theme.accent
                border.width: 2
                visible: root.pictureLoaded
            }
        }

        BSpinner {
            x: (flick.width - width) / 2
            y: (flick.height - height) / 2
            size: Theme.iconSizeLg
            // (Not over a drawing shown while its other theme or version loads.)
            running: picture.status === Image.Loading && !(picture.retainWhileLoading && picture.implicitWidth > 0)
            visible: running
        }
        Text {
            objectName: "pictureMessage" // (the shell's self-test)
            visible: !root.decodable || picture.status === Image.Error
            x: (flick.width - width) / 2
            y: (flick.height - height) / 2
            width: Math.min(flick.width - 2 * Theme.space6, 480)
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: !root.decodable ? qsTr("Pictures in the %1 format cannot be shown on this device.").arg(root.formatName)
                                  : qsTr("Cannot load %1").arg(root.fileName)
            textFormat: Text.PlainText
            font.family: Theme.fontUi
            font.pixelSize: Theme.textSm
            color: Theme.fgMuted
        }

        // The notes that embed or link the picture (none: nothing here).
        ContentBacklinks {
            id: refs
            objectName: "pictureReferences" // (the shell's self-test)
            visible: !root.actualSize && !empty
            x: Theme.compact ? Theme.space4 : Theme.space8
            y: picture.y + picture.height + Theme.space8
            width: Math.min(flick.width - 2 * x, 760)
            path: root.path
            rendererPalette: root.rendererPalette
            compact: Theme.compact
            showUnlinked: false

            Rectangle {
                // A rule between the picture and its references.
                y: -Theme.space4
                width: parent.width
                height: 1
                color: Theme.borderSubtle
            }
        }
    }

    // What OCR read in the picture: read-only, selectable.
    Loader {
        anchors.fill: parent
        anchors.topMargin: bar.height
        active: root.showRecognized && !root.asText
        sourceComponent: Flickable {
            id: recognizedFlick
            objectName: "pictureRecognized"
            clip: true
            contentWidth: width
            contentHeight: recognizedColumn.height + 2 * Theme.space6
            boundsBehavior: Flickable.StopAtBounds
            TouchFlick.enabled: true
            T.ScrollBar.vertical: BScrollBar {}

            Column {
                id: recognizedColumn
                x: Theme.compact ? Theme.space4 : Theme.space8
                y: Theme.space6
                width: Math.min(recognizedFlick.width - 2 * x, 760)
                spacing: Theme.space3

                BText {
                    width: parent.width
                    text: qsTr("Text recognized in the picture (OCR, %1). It may hold mistakes.").arg(Workspace.attachments.ocrLanguages)
                    variant: "muted"
                    wrapMode: Text.Wrap
                }
                TextEdit {
                    objectName: "pictureRecognizedText"
                    width: parent.width
                    readOnly: true
                    selectByMouse: !Theme.touchUi
                    textFormat: TextEdit.PlainText
                    wrapMode: TextEdit.Wrap
                    text: root.recognized
                    font.family: Theme.fontEditor
                    font.pixelSize: Theme.textBase
                    color: Theme.fg
                    selectionColor: Theme.selection
                    selectedTextColor: Theme.selectionFg
                    Accessible.role: Accessible.StaticText
                    Accessible.name: qsTr("Recognized text")
                }
            }
        }
    }

    // The file read as text: read-only (nothing typed can reach a picture's bytes).
    Loader {
        anchors.fill: parent
        anchors.topMargin: bar.height
        active: root.asText
        sourceComponent: Flickable {
            id: textFlick

            /// The file's start: { text, truncated, bytes / bytesText (read), size / sizeText }.
            readonly property var file: {
                // `revision` used for real: qmlcachegen compiles a bare `void revision` away, and
                // the binding's dependency on it with it. (It grows when the file changes on disk:
                // Syncthing, Nextcloud, another program; the file is read again.)
                const revision = root.fileRevision
                return revision >= 0 ? Workspace.readTextStart(root.path, root.textLimit) : ({})
            }
            readonly property bool truncated: AppController.routeFlag(file, "truncated")

            objectName: "pictureText" // (the shell's self-test)
            clip: true
            contentWidth: width
            contentHeight: textColumn.height + 2 * Theme.space6
            boundsBehavior: Flickable.StopAtBounds
            TouchFlick.enabled: true
            T.ScrollBar.vertical: BScrollBar {}

            Column {
                id: textColumn
                x: Theme.compact ? Theme.space4 : Theme.space8
                y: Theme.space6
                width: textFlick.width - 2 * x
                spacing: Theme.space3

                BText {
                    objectName: "pictureTextNote" // (the shell's self-test)
                    visible: textFlick.truncated
                    width: parent.width
                    text: qsTr("The start of the file: %1 of %2.").arg(AppController.routeText(textFlick.file, "bytesText")).arg(AppController.routeText(textFlick.file, "sizeText"))
                    variant: "muted"
                    wrapMode: Text.Wrap
                }
                TextEdit {
                    objectName: "pictureTextEdit" // (the shell's self-test)
                    width: parent.width
                    readOnly: true
                    // Not on touch UIs: a TextEdit that selects by dragging keeps the finger,
                    // and the text could not be scrolled at all.
                    selectByMouse: !Theme.touchUi
                    textFormat: TextEdit.PlainText
                    wrapMode: TextEdit.WrapAtWordBoundaryOrAnywhere
                    text: AppController.routeText(textFlick.file, "text")
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.textSm + 1
                    color: Theme.fg
                    selectionColor: Theme.selection
                    selectedTextColor: Theme.selectionFg
                    Accessible.role: Accessible.StaticText
                    Accessible.name: root.fileName
                }
            }
        }
    }
}
