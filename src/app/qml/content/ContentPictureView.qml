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
// and "Pokaż jako tekst" ⇄ "Pokaż obraz": the file read as text, read-only — a binary picture's
// bytes are mojibake there, and a typed character would have been saved over the picture; the
// view is for text-like assets such as SVG (`asText`, PageView switches it, Ctrl+E too).
// In a narrow view (a phone; on the desktop between the sidebar and the side panel) the size
// toggle is an icon, its text in the tooltip and for screen readers (as text it left the file's
// name no room).
// A picture whose format this device cannot decode (WebP or AVIF without Qt's image plugins:
// Workspace.canDecodePicture) is named so instead of a load error.
// A drawing (Workspace.isDrawing) has "Edytuj rysunek" in the bar (the drawing editor, route
// "drawing"); the picture follows the file when it is saved again, and the app's theme (light or
// dark paper, as the drawing chooses: Workspace.drawingUrl, the drawing image provider).
// The text view reads at most `textLimit` bytes of the file (Workspace.readTextStart: a photo is
// never read whole for it), again whenever the file changes (sync, another program).
//
//   ContentPictureView { path: routePath; asText: pictureAsText
//                        rendererPalette: editor.rendererPalette
//                        onTextToggleRequested: pictureAsText = !pictureAsText }
//
// The URL comes from Workspace.assetUrl (file:// on desktop/Android, data:/blob on WebAssembly),
// the pixel size from Workspace.pictureSize (the file's header, no decode): the picture's own
// size in the pixels view, and the decode box — both of its sides follow the picture's shape, so
// Qt scales it without stretching it to a box of another shape (an SVG is rendered at sourceSize
// itself). A raster picture is decoded at most 2048 px a side (a 12 MP photo would otherwise
// take 48 MB plus mipmaps) and never more than its own pixels (no PreserveAspectFit, which makes
// Qt scale a smaller picture *up* to sourceSize); an SVG is re-rendered up to 2048 px a side
// (vector: a bigger render stays sharp — a raster picture would only get a bigger item over the
// same pixels).
Item {
    id: root

    objectName: "pictureView" // (the shell's self-test)

    property string path
    /// The file read as text (read-only) instead of the picture.
    property bool asText: false
    /// Renderer colours for the references (BlockEditor.rendererPalette).
    property var rendererPalette: ({})
    /// The bar's "Pokaż jako tekst" / "Pokaż obraz" (PageView switches `asText`).
    signal textToggleRequested()

    /// Fitted into the view ↔ the picture's own pixels (the flickable then).
    property bool actualSize: false
    /// At most this many bytes of the file are read and shown as text (a photo's bytes would
    /// take seconds to lay out, for nothing anyone can read).
    property int textLimit: 200 * 1024

    readonly property string fileName: {
        const i = root.path.lastIndexOf("/")
        return i < 0 ? root.path : root.path.slice(i + 1)
    }
    readonly property bool scalable: /\.svg$/i.test(root.fileName)
    /// This device can decode the picture's format (see above).
    readonly property bool decodable: root.path !== "" && Workspace.canDecodePicture(root.path)
    /// "WEBP" for assets/łąka.webp (the message for a format this device cannot show).
    readonly property string formatName: {
        const i = root.fileName.lastIndexOf(".")
        return i < 0 ? "" : root.fileName.slice(i + 1).toUpperCase()
    }
    /// Grows when the file changes on disk (a drawing saved again): its URL is asked for again
    /// (a drawing's carries its version, Workspace.assetUrl).
    property int fileRevision: 0
    readonly property url source: !root.decodable || root.fileRevision < 0 ? ""
                                  : root.drawing ? Workspace.drawingUrl(root.path, Theme.dark)
                                  : Workspace.assetUrl(root.path)
    /// A drawing (docs/SYNTAX.md §2 "Drawings"): the bar offers the drawing editor.
    readonly property bool drawing: root.scalable && root.fileRevision >= 0 && Workspace.isDrawing(root.path)

    Connections {
        target: Workspace
        function onFileChanged(path) {
            if (path === root.path)
                root.fileRevision++
        }
    }
    /// The picture's pixels from its header (≤ 0: not in the header — the loaded picture's then).
    readonly property size headerSize: Workspace.pictureSize(root.path)
    readonly property bool headerKnown: headerSize.width > 0 && headerSize.height > 0
    /// The picture's own pixels in the pixels view (the loaded size when the header has none).
    readonly property size shownSize: headerKnown ? headerSize
                                     : Qt.size(Math.max(1, picture.implicitWidth),
                                               Math.max(1, picture.implicitHeight))
    /// What the picture is decoded at: its own shape, at most 2048 px a side (see above).
    readonly property var decodeSize: {
        if (!root.headerKnown)
            return root.scalable ? undefined : Qt.size(2048, 2048)
        const k = root.scalable ? Math.min(2048 / headerSize.width, 2048 / headerSize.height)
                                : Math.min(1, 2048 / headerSize.width, 2048 / headerSize.height)
        return Qt.size(Math.max(1, Math.round(headerSize.width * k)),
                       Math.max(1, Math.round(headerSize.height * k)))
    }
    /// The fitted picture's scale: at most 1 for a raster picture (fitting only shrinks), any
    /// for an SVG.
    readonly property real fitScale: {
        const k = Math.min(flick.width / root.shownSize.width, flick.height / root.shownSize.height)
        return root.scalable ? k : Math.min(1, k)
    }
    /// The picture is loaded and drawn (the shell's self-test).
    readonly property bool pictureLoaded: picture.status === Image.Ready && picture.paintedWidth > 0
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
        root.actualSize = false
        flick.contentX = 0
        flick.contentY = 0
    }

    /// Fitted ↔ the picture's own pixels, centred in the other view.
    function toggleSize() {
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
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Theme.borderSubtle
        }

        RowLayout {
            id: barRow

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: Theme.space4
            anchors.rightMargin: Theme.space4
            spacing: Theme.space3

            BIcon {
                name: "image"
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
            BButton {
                objectName: "pictureTextButton"
                size: "small"
                variant: "ghost"
                iconName: root.asText ? "image" : "code"
                text: root.asText ? qsTr("Show picture") : qsTr("Show as text")
                Layout.alignment: Qt.AlignVCenter
                onClicked: root.textToggleRequested()
            }
        }
    }

    Flickable {
        id: flick

        anchors.top: bar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        visible: !root.asText
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
            width: root.actualSize ? root.shownSize.width : Math.max(1, Math.round(root.shownSize.width * root.fitScale))
            height: root.actualSize ? root.shownSize.height : Math.max(1, Math.round(root.shownSize.height * root.fitScale))
            source: root.asText ? "" : root.source
            sourceSize: root.decodeSize
            // A drawing's other theme or version: the shown one stays until it is there.
            retainWhileLoading: root.drawing
            fillMode: root.scalable ? Image.PreserveAspectFit : Image.Stretch
            asynchronous: true
            smooth: true
            mipmap: true
            Accessible.role: Accessible.Graphic
            Accessible.name: root.fileName

            TapHandler { onTapped: root.toggleSize() }
            HoverHandler {
                cursorShape: root.canZoom || root.actualSize ? Qt.PointingHandCursor : Qt.ArrowCursor
                enabled: !Theme.touchUi
            }
        }

        BSpinner {
            x: (flick.width - width) / 2
            y: (flick.height - height) / 2
            size: Theme.iconSizeLg
            running: picture.status === Image.Loading
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

    // The file read as text: read-only (nothing typed can reach a picture's bytes).
    Loader {
        anchors.top: bar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        active: root.asText
        sourceComponent: Flickable {
            id: textFlick

            /// Grows when the file changes on disk: it is read again.
            property int revision: 0
            /// The file's start: { text, truncated, bytes / bytesText (read), size / sizeText }.
            readonly property var file: {
                // `revision` used for real: qmlcachegen compiles a bare `void revision` away, and
                // the binding's dependency on it with it.
                const revision = textFlick.revision
                return revision >= 0 ? Workspace.readTextStart(root.path, root.textLimit) : ({})
            }
            readonly property bool truncated: file.truncated === true

            objectName: "pictureText" // (the shell's self-test)
            clip: true
            contentWidth: width
            contentHeight: textColumn.height + 2 * Theme.space6
            boundsBehavior: Flickable.StopAtBounds
            TouchFlick.enabled: true
            T.ScrollBar.vertical: BScrollBar {}

            // The file changed on disk while shown (Syncthing, Nextcloud, another program).
            Connections {
                target: Workspace
                function onFileChanged(path) {
                    if (path === root.path)
                        textFlick.revision++
                }
            }

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
                    text: qsTr("The start of the file: %1 of %2.").arg(textFlick.file.bytesText).arg(textFlick.file.sizeText)
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
                    text: textFlick.file.text || ""
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
