pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ContentPictureView — a picture attachment opened as a page (assets/photo.png): the picture
// itself, fitted into the view with its aspect ratio kept. A click on the picture — or the size
// button in the bar — switches to the picture's own pixels, where it can be dragged around; the
// button switches back. The bar holds the file's name, the size toggle and "Pokaż jako tekst":
// the alternative view — the file read as text in the block editor (PageView.showPictureAsText;
// a binary picture's bytes are mojibake there, the view stays for the old behaviour and for
// text-like assets).
//
//   ContentPictureView { path: pictureRoute ? routePath : ""; onShowTextRequested: … }
//
// The URL comes from Workspace.assetUrl (file:// on desktop/Android, data:/blob on WebAssembly),
// the pixel size from Workspace.pictureSize (the file's header, no decode): the picture's own
// size in the pixels view, and the decode box — both of its sides follow the picture's shape, so
// Qt scales it without stretching it to a box of another shape (an SVG is rendered at sourceSize
// itself). A raster picture is decoded at most 2048 px a side (a 12 MP photo would otherwise
// take 48 MB plus mipmaps); an SVG is re-rendered up to 2048 px a side (vector: a bigger render
// stays sharp — a raster picture would only get a bigger item over the same pixels).
Item {
    id: root

    objectName: "pictureView" // (the shell's self-test)

    property string path
    /// The file's text in the editor was asked for (PageView switches its views).
    signal showTextRequested()

    /// Fitted into the view ↔ the picture's own pixels (the flickable then).
    property bool actualSize: false

    readonly property string fileName: {
        const i = root.path.lastIndexOf("/")
        return i < 0 ? root.path : root.path.slice(i + 1)
    }
    readonly property bool scalable: /\.svgz?$/i.test(root.fileName)
    readonly property url source: root.path !== "" ? Workspace.assetUrl(root.path) : ""
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
    /// The picture is loaded and drawn (the shell's self-test).
    readonly property bool pictureLoaded: picture.status === Image.Ready && picture.paintedWidth > 0

    // Another picture: start fitted from the top left (the flickable clamps itself to the
    // smaller content, but contentX/Y are plain state without a binding to reset them).
    onPathChanged: {
        root.actualSize = false
        flick.contentX = 0
        flick.contentY = 0
    }

    /// Fitted ↔ the picture's own pixels, centred in the other view.
    function toggleSize() {
        root.actualSize = !root.actualSize
        flick.contentX = Math.max(0, (flick.contentWidth - flick.width) / 2)
        flick.contentY = Math.max(0, (flick.contentHeight - flick.height) / 2)
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
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.alignment: Qt.AlignVCenter
                text: root.fileName
                elide: Text.ElideMiddle
                font.family: Theme.fontUi
                font.pixelSize: Theme.textSm
                color: Theme.fgMuted
            }
            BButton {
                objectName: "pictureSizeButton"
                size: "small"
                variant: "ghost"
                text: root.actualSize ? qsTr("Fit to view") : qsTr("Zoom to actual size")
                Layout.alignment: Qt.AlignVCenter
                onClicked: root.toggleSize()
            }
            BButton {
                objectName: "pictureTextButton"
                size: "small"
                variant: "ghost"
                iconName: "code"
                text: qsTr("Show as text")
                Layout.alignment: Qt.AlignVCenter
                onClicked: root.showTextRequested()
            }
        }
    }

    Flickable {
        id: flick

        anchors.top: bar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        clip: true
        contentWidth: picture.width
        contentHeight: picture.height
        interactive: root.actualSize
        boundsBehavior: Flickable.StopAtBounds

        Image {
            id: picture

            objectName: "pictureImage" // (the shell's self-test)
            x: root.actualSize ? Math.max(0, (flick.width - width) / 2) : 0
            y: root.actualSize ? Math.max(0, (flick.height - height) / 2) : 0
            width: root.actualSize ? root.shownSize.width : flick.width
            height: root.actualSize ? root.shownSize.height : flick.height
            source: root.source
            sourceSize: root.decodeSize
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            smooth: true
            mipmap: true
            Accessible.role: Accessible.Graphic
            Accessible.name: root.fileName

            TapHandler { onTapped: root.toggleSize() }
            HoverHandler {
                cursorShape: Qt.PointingHandCursor
                enabled: !Theme.touchUi
            }
        }

        BSpinner {
            anchors.centerIn: parent
            size: Theme.iconSizeLg
            running: picture.status === Image.Loading
            visible: running
        }
        Text {
            visible: picture.status === Image.Error
            anchors.centerIn: parent
            text: qsTr("Cannot load %1").arg(root.fileName)
            textFormat: Text.PlainText
            font.family: Theme.fontUi
            font.pixelSize: Theme.textSm
            color: Theme.fgMuted
        }
    }
}
