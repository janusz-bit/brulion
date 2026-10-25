pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// WebPictureMenu — the menu of a picture from the internet in a block (`![alt](https://…)`,
// EmbedView): a click, right-click or long press on the shown picture. Store it in the notebook
// (PageController.downloadWebPictures: an attachment, the link points at it), open its address
// in the browser, copy the address.
//
//   WebPictureMenu { host: "example.com"; onChosen: (action) => … }   // "download" | "open" | "copy"
BMenu {
    id: menu

    /// The picture's host, shown as the menu's title.
    property string host

    signal chosen(string action)

    title: qsTr("Picture from %1").arg(host)

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
