pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import QtQuick.Templates as T
import Brulion

// ContentPageCoverDialog — the page's cover (docs/UI.md "Page icon and cover"): a picture of the
// notebook (its pictures as thumbnails, searchable by name), one from a file of the device ("Z
// pliku…": stored as an attachment, as an inserted picture is), or a picture from the internet by
// its address — shown only through the web-picture cache, after "Pokaż" unless the device allows
// pictures from the internet. A pick is written at once (PageController.setPageCover: the page's
// `banner` property, `![[file]]` or the URL; one undo step, "Cofnij" in the toast); "Usuń okładkę"
// removes it with its position.
//
//   ContentPageCoverDialog { id: coverDialog; controller: page; editor: editor }   coverDialog.open()
BDialog {
    id: dialog

    property PageController controller
    property BlockEditor editor
    /// A map a call returned, read with AppController.route*() (compiled calls cannot take a map
    /// held in a local after another call: kept here).
    property var _held: ({})
    /// The page's cover now (PageController.look.cover: kind "" | "file" | "web", target).
    readonly property string coverKind: dialog.controller ? AppController.routeText(dialog.controller.look.cover, "kind") : ""
    readonly property string coverTarget: dialog.controller ? AppController.routeText(dialog.controller.look.cover, "target") : ""
    readonly property bool hasCover: coverKind !== ""
    /// The notebook's pictures matching the search (Workspace.pictureAttachments), read while open.
    readonly property list<var> pictures: dialog.opened && Workspace.revision >= 0
                                    ? Workspace.pictureAttachments(searchField.text, 60) : []

    /// Write the cover `target` (a vault path or a URL; "" removes it) and close.
    function pick(target: string): void {
        if (!dialog.controller)
            return
        // (Strings from calls laundered: a call cannot take one a call before may change.)
        const before = "" + dialog.controller.undoToken()
        dialog._held = dialog.controller.setPageCover(target)
        if (!AppController.routeFlag(dialog._held, "ok")) {
            AppController.toast(AppController.routeText(dialog._held, "reason"), "error")
            return
        }
        dialog.close()
        if (AppController.routeText(dialog._held, "status") !== "changed")
            return
        const text = target === "" ? qsTr("Cover removed") : qsTr("Cover set")
        if (dialog.editor)
            dialog.editor.noticeUndoable(text, "success", before)
        else
            AppController.toast(text, "success")
    }

    title: qsTr("Page cover")
    iconName: "image"
    showFooter: true
    acceptText: qsTr("Use address")
    acceptEnabled: Js.test(Js.trim(urlField.text), "^https?:\\/\\/\\S+$", "i")
    initialFocusItem: Theme.touchUi ? null : searchField
    onAccepted: dialog.pick(Js.trim(urlField.text))
    onAboutToShow: {
        searchField.text = ""
        urlField.text = dialog.coverKind === "web" ? dialog.coverTarget : ""
    }
    footerLeading: BButton {
        objectName: "pageCoverRemove"
        visible: dialog.hasCover
        variant: "ghost"
        text: qsTr("Remove cover")
        iconName: "trash-2"
        onClicked: dialog.pick("")
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.space2
        BSearchField {
            id: searchField
            objectName: "pageCoverSearch"
            Layout.fillWidth: true
            placeholderText: qsTr("Find a picture in the notebook")
        }
        BButton {
            objectName: "pageCoverFile"
            text: qsTr("From file…")
            iconName: "upload"
            onClicked: filePicker.open()
        }
    }
    Flow {
        objectName: "pageCoverPictures"
        Layout.fillWidth: true
        spacing: Theme.space2
        Repeater {
            model: dialog.pictures
            delegate: T.AbstractButton {
                id: tile
                required property string path
                required property string title
                required property url url
                width: Theme.touchUi ? 104 : 120
                height: width * 0.7
                focusPolicy: Qt.StrongFocus
                hoverEnabled: !Theme.touchUi
                Accessible.role: Accessible.Button
                Accessible.name: tile.title
                onClicked: dialog.pick(tile.path)
                background: Rectangle {
                    radius: Theme.radius
                    color: Theme.bgSunken
                    border.width: tile.visualFocus || tile.hovered || dialog.coverTarget === tile.title ? 2 : 1
                    border.color: tile.visualFocus ? Theme.focusRing : tile.hovered ? Theme.accent : Theme.border
                }
                contentItem: Image {
                    source: tile.url
                    asynchronous: true
                    fillMode: Image.PreserveAspectCrop
                    sourceSize.width: Math.ceil(tile.width * Screen.devicePixelRatio)
                    clip: true
                }
                Loader {
                    active: tile.hovered
                    sourceComponent: BToolTip {
                        parent: tile
                        text: tile.path
                    }
                }
            }
        }
    }
    BText {
        visible: dialog.pictures.length === 0
        Layout.fillWidth: true
        text: Js.trim(searchField.text) !== "" ? qsTr("No pictures match") : qsTr("The notebook has no pictures yet")
        variant: "muted"
        wrapMode: Text.Wrap
    }
    BSectionHeader {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space2
        text: qsTr("Picture from the internet")
    }
    BTextField {
        id: urlField
        objectName: "pageCoverUrl"
        Layout.fillWidth: true
        placeholderText: "https://…"
        accessibleName: qsTr("Picture address")
        inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase
        onAccepted: if (dialog.acceptEnabled) dialog.pick(Js.trim(text))
    }
    BText {
        Layout.fillWidth: true
        text: qsTr("It is fetched only when you allow it (“Show” on the cover), and kept on this device.")
        variant: "small"
        color: Theme.fgMuted
        wrapMode: Text.Wrap
    }

    FilePicker {
        id: filePicker
        title: qsTr("Choose a cover picture")
        fileMode: FilePicker.OpenFile
        dark: Theme.dark
        colors: Theme.colors
        nameFilters: dialog.controller ? dialog.controller.imageNameFilters : []
        onAccepted: {
            const path = Workspace.importAttachment(selectedFile)
            if (path === "")
                AppController.toast(qsTr("The picture could not be stored in the notebook"), "error")
            else
                dialog.pick(path)
        }
    }
}
