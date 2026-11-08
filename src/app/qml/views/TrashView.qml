pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// TrashView — route "trash": what was moved to the trash (the vault's .trash/ folder, so another
// device syncing the folder sees the same), most recently deleted first — a deleted folder is
// one entry. Each can be restored to where it was (Workspace.restore; links to it work again) or
// deleted for good after a confirmation; "Empty trash" deletes everything (confirmed).
// A row shows "Restore" (on compact windows its icon) and a "⋯" whose menu — also from a
// right-click or a long press on the row — has Restore and, set apart at its end in the danger
// colour, "Delete for good…" (as the All pages and tree rows: no bare red × that deletes). The
// header's "Empty trash" is a secondary button with danger text; only the confirmations are
// solid red.
// The list follows Workspace.trashChanged (deletions anywhere, Syncthing).
// Contract with the shell (qml/shell/ShellRouteView.qml): `route`, optional `title`.
FocusScope {
    id: root

    property var route: ({})
    readonly property int maximumWidth: 860
    readonly property real sideMargin: Math.max(Theme.compact ? Theme.space3 : Theme.space6, (width - maximumWidth) / 2)
    // Workspace.trashItems(): rows { trashPath, original, title, folder, files, size, deletedAt, when }.
    property list<var> items: []

    function reload(): void {
        items = Workspace.trashItems()
    }
    function sizeLabel(bytes: real): string {
        const locale = Qt.locale(DatesHelper.locale === "en" ? "en_US" : "pl_PL")
        if (bytes < 1024)
            return qsTr("%1 B").arg(bytes)
        if (bytes < 1024 * 1024)
            return qsTr("%1 kB").arg(locale.toString(bytes / 1024, "f", bytes < 10240 ? 1 : 0))
        return qsTr("%1 MB").arg(locale.toString(bytes / (1024 * 1024), "f", 1))
    }
    function nonEmpty(parts: list<string>): list<string> {
        return Js.nonEmptyStrings(parts)
    }
    function restore(trashPath: string, title: string): void {
        const path = "" + Workspace.restore(trashPath)
        if (path === "") {
            AppController.toast(qsTr("“%1” could not be restored.").arg(title), "error")
            return
        }
        const folder = Workspace.folderOf(path)
        AppController.toast(folder !== "" ? qsTr("“%1” is back in the folder %2").arg(title).arg(folder)
                                          : qsTr("“%1” is back in the notebook").arg(title), "success")
    }

    focus: true
    Component.onCompleted: reload()

    Connections {
        target: Workspace
        function onTrashChanged() { root.reload() }
        function onOpenChanged() { root.reload() }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.bg
    }

    ListView {
        id: list
        TouchFlick.enabled: true
        anchors.fill: parent
        focus: true
        clip: true
        model: root.items
        currentIndex: -1
        keyNavigationEnabled: true
        boundsBehavior: Flickable.StopAtBounds
        bottomMargin: Theme.narrow ? Theme.space16 + Theme.space8 : Theme.space8
        T.ScrollBar.vertical: BScrollBar {}
        Accessible.role: Accessible.List
        Accessible.name: qsTr("Trash")

        header: Item {
            width: list.width
            implicitHeight: head.y + head.implicitHeight + Theme.space4

            RowLayout {
                id: head
                x: root.sideMargin
                y: Theme.compact ? Theme.space3 : Theme.space6
                width: list.width - 2 * root.sideMargin
                spacing: Theme.space3
                BText {
                    Layout.fillWidth: true
                    text: root.items.length > 0 ? qsTr("%n item(s) in the trash", "", root.items.length) : ""
                    variant: "muted"
                    wrapMode: Text.Wrap
                }
                BButton {
                    objectName: "trashEmpty"
                    visible: root.items.length > 0
                    iconName: "trash-2"
                    textColor: Theme.dangerText
                    text: qsTr("Empty trash")
                    onClicked: emptyDialog.open()
                }
            }
        }

        delegate: Item {
            id: entry
            required property int index
            required property string trashPath
            required property string original
            required property string title
            required property bool folder
            required property int files
            required property real size
            required property string when
            readonly property string journalDay: folder ? "" : Workspace.journalDate(original)
            readonly property string shownTitle: journalDay !== "" ? DatesHelper.formatLong(journalDay) : title
            readonly property string fromFolder: Workspace.folderOf(original)

            width: list.width
            implicitHeight: Math.max(Theme.listItemHeightTwoLine, rowLayout.implicitHeight + 2 * Theme.space2)

            /// The row's menu at (x, y) of `item`.
            function openMenu(item: Item, x: real, y: real): void {
                rowMenu.openFor(entry.trashPath, entry.title, entry.shownTitle, entry.folder, entry.files, item, x, y)
            }

            Rectangle {
                x: root.sideMargin - Theme.space2
                width: list.width - 2 * root.sideMargin + 2 * Theme.space2
                height: parent.height
                radius: Theme.radius
                color: entryHover.hovered ? Theme.bgHover : "transparent"
                HoverHandler { id: entryHover; enabled: !Theme.touchUi }
            }
            TapHandler {
                // Right-click only: Qt gives a finger (no buttons) to every TapHandler.
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
                acceptedButtons: Qt.RightButton
                onTapped: (eventPoint) => entry.openMenu(entry, eventPoint.position.x, eventPoint.position.y)
            }
            TapHandler {
                // A finger held on the row (a passive grab: a flick of the list goes on).
                id: rowHold
                acceptedDevices: PointerDevice.TouchScreen
                onLongPressed: entry.openMenu(entry, rowHold.point.position.x, rowHold.point.position.y)
            }

            RowLayout {
                id: rowLayout
                x: root.sideMargin
                width: list.width - 2 * root.sideMargin
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.space3

                BIcon {
                    name: entry.folder ? "folder" : entry.journalDay !== "" ? "calendar-days" : "file-text"
                    size: Theme.iconSize
                    color: Theme.fgMuted
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: 2
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    BText {
                        Layout.fillWidth: true
                        text: entry.shownTitle
                        font.weight: Theme.weightMedium
                        elide: Text.ElideRight
                    }
                    BText {
                        Layout.fillWidth: true
                        text: root.nonEmpty([entry.journalDay !== "" ? qsTr("journal")
                                             : entry.fromFolder !== "" ? qsTr("from %1").arg(entry.fromFolder) : "",
                                             entry.when !== "" ? qsTr("deleted %1").arg(entry.when) : "",
                                             entry.folder ? qsTr("%n file(s)", "", entry.files) : "",
                                             root.sizeLabel(entry.size)]).join(" · ")
                        variant: "small"
                        color: Theme.fgMuted
                        wrapMode: Text.Wrap
                    }
                }
                BButton {
                    objectName: "trashRestore"
                    size: "small"
                    iconName: "undo-2"
                    text: Theme.compact ? "" : qsTr("Restore")
                    Accessible.name: qsTr("Restore %1").arg(entry.shownTitle)
                    onClicked: root.restore(entry.trashPath, entry.title)
                }
                BIconButton {
                    id: moreButton
                    objectName: "trashRowMore"
                    size: "small"
                    iconName: "ellipsis"
                    label: qsTr("More")
                    Accessible.name: qsTr("More: %1").arg(entry.shownTitle)
                    onClicked: entry.openMenu(moreButton, 0, moreButton.height)
                }
            }
        }

        BEmptyState {
            parent: list
            anchors.centerIn: parent
            width: Math.min(parent.width - 2 * Theme.space6, implicitWidth)
            visible: root.items.length === 0
            iconName: "trash-2"
            title: qsTr("The trash is empty")
            description: qsTr("Deleted pages land here first and can be restored. Version history keeps older texts of pages on this device too.")
        }
    }

    // A row's menu: Restore, and "Delete for good…" set apart at its end (BMenuItem's rule for
    // destructive items).
    BMenu {
        id: rowMenu
        objectName: "trashRowMenu"
        property string itemTrashPath
        property string itemTitle
        property string itemShownTitle
        property bool itemFolder
        property int itemFiles
        function openFor(trashPath: string, title: string, shownTitle: string, folder: bool, files: int,
                         item: Item, x: real, y: real): void {
            itemTrashPath = trashPath
            itemTitle = title
            itemShownTitle = shownTitle
            itemFolder = folder
            itemFiles = files
            popup(item, x, y)
        }
        BMenuItem {
            objectName: "trashMenuRestore"
            text: qsTr("Restore")
            iconName: "undo-2"
            Accessible.name: qsTr("Restore %1").arg(rowMenu.itemShownTitle)
            onTriggered: root.restore(rowMenu.itemTrashPath, rowMenu.itemTitle)
        }
        BMenuSeparator {}
        BMenuItem {
            objectName: "trashMenuDelete"
            text: qsTr("Delete for good…")
            iconName: "trash-2"
            danger: true
            Accessible.name: qsTr("Delete %1 for good").arg(rowMenu.itemShownTitle)
            onTriggered: deleteDialog.openFor(rowMenu.itemTrashPath, rowMenu.itemTitle, rowMenu.itemFolder,
                                              rowMenu.itemFiles)
        }
    }

    BConfirmDialog {
        id: deleteDialog
        property string itemTrashPath
        property string itemTitle
        property bool itemFolder
        property int itemFiles
        function openFor(trashPath: string, title: string, folder: bool, files: int): void {
            itemTrashPath = trashPath
            itemTitle = title
            itemFolder = folder
            itemFiles = files
            open()
        }
        parent: T.Overlay.overlay
        title: qsTr("Delete for good?")
        message: deleteDialog.itemFolder
                 ? qsTr("The folder “%1” and the %n file(s) in it will be deleted permanently. This cannot be undone.", "", deleteDialog.itemFiles).arg(deleteDialog.itemTitle)
                 : qsTr("“%1” will be deleted permanently. This cannot be undone.").arg(deleteDialog.itemTitle)
        confirmText: qsTr("Delete")
        destructive: true
        onAccepted: {
            if (Workspace.deleteForever(deleteDialog.itemTrashPath))
                AppController.toast(qsTr("Deleted"), "info")
        }
    }
    BConfirmDialog {
        id: emptyDialog
        parent: T.Overlay.overlay
        title: qsTr("Empty the trash?")
        message: qsTr("%n item(s) will be deleted permanently. This cannot be undone.", "", root.items.length)
        confirmText: qsTr("Empty trash")
        destructive: true
        onAccepted: {
            if (Workspace.emptyTrash())
                AppController.toast(qsTr("The trash is empty"), "info")
        }
    }
}
