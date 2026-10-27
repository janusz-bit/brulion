pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// WebPictureMenu — the menu of a picture from the internet in a block (`![alt](https://…)`,
// EmbedView): a right-click or long press on the shown picture, "⋯" in its viewer. View it on its
// own (WebPictureViewer; not offered in the viewer itself), store it in the notebook
// (PageController.downloadWebPictures: an attachment, the link points at it), open its address
// in the browser, copy the address. Its first line names the site (BMenu shows no title).
//
//   WebPictureMenu { host: "example.com"; onChosen: (action) => … }   // "view" | "download" | "open" | "copy"
BMenu {
    id: menu

    /// The picture's host, shown on the menu's first line.
    property string host
    /// Offer "Powiększ" (not from the viewer itself).
    property bool canView: true

    signal chosen(string action)

    BMenuItem {
        objectName: "webPictureHost"
        enabled: false
        text: qsTr("Picture from %1").arg(menu.host)
        iconName: "image"
    }
    BMenuSeparator { objectName: "webPictureHostLine" }
    BMenuItem {
        objectName: "webPictureView"
        visible: menu.canView
        height: visible ? implicitHeight : 0
        text: qsTr("View picture")
        iconName: "zoom-in"
        onTriggered: menu.chosen("view")
    }
    BMenuItem {
        objectName: "webPictureDownload"
        text: qsTr("Save to notebook")
        iconName: "download"
        onTriggered: menu.chosen("download")
    }
    BMenuItem {
        objectName: "webPictureOpen"
        text: qsTr("Open in browser")
        iconName: "external-link"
        onTriggered: menu.chosen("open")
    }
    BMenuItem {
        objectName: "webPictureCopy"
        text: qsTr("Copy picture address")
        iconName: "copy"
        onTriggered: menu.chosen("copy")
    }
}
