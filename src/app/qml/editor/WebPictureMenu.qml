pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// WebPictureMenu — the menu of a picture from the internet in a block (`![alt](https://…)`,
// EmbedView): a click, right-click or long press on the shown picture. Store it in the notebook
// (PageController.downloadWebPictures: an attachment, the link points at it), open its address
// in the browser, copy the address. Its first line names the site (BMenu shows no title).
//
//   WebPictureMenu { host: "example.com"; onChosen: (action) => … }   // "download" | "open" | "copy"
BMenu {
    id: menu

    /// The picture's host, shown on the menu's first line.
    property string host

    signal chosen(string action)

    BMenuItem {
        objectName: "webPictureHost"
        enabled: false
        text: qsTr("Picture from %1").arg(menu.host)
        iconName: "image"
    }
    BMenuSeparator { objectName: "webPictureHostLine" }
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
