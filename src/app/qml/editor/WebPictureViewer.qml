pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// WebPictureViewer — a picture from the internet (`![alt](https://…)`) on its own, over the whole
// window: a click on the shown picture in a block (EmbedView.webOpen) or "Powiększ" in its menu.
// Drawn like an attachment in the picture view (ContentPictureView): fitted into the window with
// its aspect ratio kept (fitting only shrinks a raster picture; an SVG is drawn to fill), and when
// the fitted picture differs from its own pixels a click on it — or the size button — shows those
// pixels, dragged around. No references and no text view: the picture is no file of the notebook.
// The bar names the site and the file (webpicture::displayName, plain text: a name from anywhere
// is no markup) and holds the size toggle, "Zapisz w notatniku" (saveRequested), the picture's
// menu (menuRequested: WebPictureMenu) and "Zamknij"; Escape and Back (Android) close it too.
// The picture is the device's cached copy (the embed's `url`: file:// or a data: URL in the
// browser) — QML loads nothing from the network; its pixel size comes with the embed (the cached
// file's header), else from the loaded picture. Decoded at most 2048 px a side, an SVG rendered
// up to 2048 px (as in the picture view).
//
//   WebPictureViewer { picture: embed.modelData
//                      onSaveRequested: editor.downloadWebPictures(picture.target)
//                      onMenuRequested: (item) => editor.openWebPictureMenu(…) }
T.Popup {
    id: viewer

    objectName: "webPictureViewer"

    /// The embed (BlockListModel's web embed): { target, alt, host, name, url, width, height,
    /// format, decodable, state }.
    property var picture: ({})
    /// Fitted into the window ↔ the picture's own pixels (the flickable then).
    property bool actualSize: false

    /// "Zapisz w notatniku" in the bar.
    signal saveRequested()
    /// "⋯" in the bar: the picture's menu at `item`.
    signal menuRequested(Item item)

    readonly property bool scalable: /^svgz?$/i.test((picture && picture.format) || "")
    readonly property bool decodable: !!picture && picture.decodable !== false
    readonly property url source: picture && picture.state === "ready" && decodable ? picture.url : ""
    readonly property bool sizeKnown: !!picture && (picture.width || 0) > 0 && (picture.height || 0) > 0
    /// The picture's own pixels (the loaded size when the embed does not know them).
    readonly property size shownSize: sizeKnown ? Qt.size(picture.width, picture.height)
                                     : Qt.size(Math.max(1, image.implicitWidth), Math.max(1, image.implicitHeight))
    /// What the picture is decoded at: its own shape, at most 2048 px a side.
    readonly property var decodeSize: {
        if (!sizeKnown)
            return scalable ? undefined : Qt.size(2048, 2048)
        const k = scalable ? Math.min(2048 / picture.width, 2048 / picture.height)
                           : Math.min(1, 2048 / picture.width, 2048 / picture.height)
        return Qt.size(Math.max(1, Math.round(picture.width * k)), Math.max(1, Math.round(picture.height * k)))
    }
    readonly property real fitScale: {
        const k = Math.min(flick.width / shownSize.width, flick.height / shownSize.height)
        return scalable ? k : Math.min(1, k)
    }
    readonly property bool pictureLoaded: image.status === Image.Ready && image.paintedWidth > 0
    readonly property bool canZoom: pictureLoaded && Math.abs(fitScale - 1) > 0.01
    readonly property string sizeToggleText: actualSize ? qsTranslate("ContentPictureView", "Fit to view")
                                                        : qsTranslate("ContentPictureView", "Zoom to actual size")
    readonly property string sizeToggleIcon: (fitScale < 1) !== actualSize ? "zoom-in" : "zoom-out"
    readonly property real safeTop: parent && parent.SafeArea ? parent.SafeArea.margins.top : 0
    readonly property real safeBottom: parent && parent.SafeArea ? parent.SafeArea.margins.bottom : 0

    /// Fitted ↔ the picture's own pixels, centred in the other view.
    function toggleSize() {
        if (!canZoom && !actualSize)
            return
        actualSize = !actualSize
        flick.contentX = Math.max(0, (flick.contentWidth - flick.width) / 2)
        flick.contentY = Math.max(0, (flick.contentHeight - flick.height) / 2)
    }

    parent: T.Overlay.overlay
    modal: true
    dim: false
    focus: true
    closePolicy: T.Popup.CloseOnEscape
    x: 0
    y: 0
    width: parent ? parent.width : Theme.windowWidth
    height: parent ? parent.height : Theme.windowHeight
    padding: 0

    onAboutToShow: {
        actualSize = false
        flick.contentX = 0
        flick.contentY = 0
    }

    background: Rectangle { color: Theme.bg }

    contentItem: Item {
        // The bar: the picture's name and the view's controls.
        Rectangle {
            id: bar

            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            implicitHeight: barRow.implicitHeight + Theme.space3 * 2 + viewer.safeTop
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
                anchors.bottom: parent.bottom
                anchors.bottomMargin: Theme.space3
                anchors.leftMargin: Theme.compact ? Theme.space3 : Theme.space4
                anchors.rightMargin: Theme.compact ? Theme.space2 : Theme.space4
                spacing: Theme.compact ? Theme.space1 : Theme.space3

                BIcon {
                    visible: !Theme.compact
                    name: "image"
                    size: Theme.iconSize
                    color: Theme.fgMuted
                    Layout.alignment: Qt.AlignVCenter
                }
                Text {
                    objectName: "webPictureViewerName"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.alignment: Qt.AlignVCenter
                    text: (viewer.picture && viewer.picture.name) || ""
                    textFormat: Text.PlainText // a name from anywhere is no markup
                    elide: Text.ElideMiddle
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.textSm
                    color: Theme.fgMuted
                }
                BIconButton {
                    objectName: "webPictureViewerSize"
                    visible: viewer.canZoom || viewer.actualSize
                    size: "small"
                    iconName: viewer.sizeToggleIcon
                    label: viewer.sizeToggleText
                    Layout.alignment: Qt.AlignVCenter
                    onClicked: viewer.toggleSize()
                }
                // Text where there is room, an icon on a phone (the name keeps some room).
                BButton {
                    objectName: "webPictureViewerSave"
                    visible: !Theme.compact
                    size: "small"
                    variant: "ghost"
                    iconName: "download"
                    text: qsTr("Save to notebook")
                    Layout.alignment: Qt.AlignVCenter
                    onClicked: viewer.saveRequested()
                }
                BIconButton {
                    visible: Theme.compact
                    size: "small"
                    iconName: "download"
                    label: qsTr("Save to notebook")
                    Layout.alignment: Qt.AlignVCenter
                    onClicked: viewer.saveRequested()
                }
                BIconButton {
                    id: moreButton
                    objectName: "webPictureViewerMore"
                    size: "small"
                    iconName: "ellipsis"
                    label: qsTr("More")
                    Layout.alignment: Qt.AlignVCenter
                    onClicked: viewer.menuRequested(moreButton)
                }
                BIconButton {
                    objectName: "webPictureViewerClose"
                    size: "small"
                    iconName: "x"
                    label: qsTr("Close")
                    Layout.alignment: Qt.AlignVCenter
                    onClicked: viewer.close()
                }
            }
        }

        Flickable {
            id: flick

            anchors.top: bar.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.bottomMargin: viewer.safeBottom
            clip: true
            contentWidth: viewer.actualSize ? Math.max(width, image.width) : width
            contentHeight: viewer.actualSize ? Math.max(height, image.height) : height
            interactive: viewer.actualSize
            boundsBehavior: Flickable.StopAtBounds
            TouchFlick.enabled: !viewer.actualSize

            Image {
                id: image

                objectName: "webPictureViewerImage"
                x: Math.max(0, (flick.width - width) / 2)
                y: Math.max(0, (flick.height - height) / 2)
                width: viewer.actualSize ? viewer.shownSize.width
                                         : Math.max(1, Math.round(viewer.shownSize.width * viewer.fitScale))
                height: viewer.actualSize ? viewer.shownSize.height
                                          : Math.max(1, Math.round(viewer.shownSize.height * viewer.fitScale))
                source: viewer.visible ? viewer.source : ""
                sourceSize: viewer.decodeSize
                fillMode: viewer.scalable ? Image.PreserveAspectFit : Image.Stretch
                asynchronous: true
                smooth: true
                mipmap: true
                Accessible.role: Accessible.Graphic
                Accessible.name: viewer.picture && viewer.picture.alt ? viewer.picture.alt
                                                                       : ((viewer.picture && viewer.picture.name) || "")

                TapHandler { onTapped: viewer.toggleSize() }
                HoverHandler {
                    cursorShape: viewer.canZoom || viewer.actualSize ? Qt.PointingHandCursor : Qt.ArrowCursor
                    enabled: !Theme.touchUi
                }
            }

            BSpinner {
                x: (flick.width - width) / 2
                y: (flick.height - height) / 2
                size: Theme.iconSizeLg
                running: image.status === Image.Loading
                visible: running
            }
            Text {
                objectName: "webPictureViewerMessage"
                visible: !viewer.decodable || viewer.source.toString() === "" || image.status === Image.Error
                x: (flick.width - width) / 2
                y: (flick.height - height) / 2
                width: Math.min(flick.width - 2 * Theme.space6, 480)
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                text: !viewer.decodable
                      ? qsTranslate("ContentPictureView", "Pictures in the %1 format cannot be shown on this device.")
                            .arg((viewer.picture && viewer.picture.format) || "")
                      : qsTranslate("ContentPictureView", "Cannot load %1").arg((viewer.picture && viewer.picture.name) || "")
                textFormat: Text.PlainText
                font.family: Theme.fontUi
                font.pixelSize: Theme.textSm
                color: Theme.fgMuted
            }
        }
    }
}
