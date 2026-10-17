pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// TrashView — route "trash": what was moved to the trash (the vault's .trash/ folder, so another
// device syncing the folder sees the same), most recently deleted first — a deleted folder is
// one entry. Each can be restored to where it was (Workspace.restore; links to it work again) or
// deleted for good after a confirmation; "Opróżnij kosz" deletes everything (confirmed).
// The list follows Workspace.trashChanged (deletions anywhere, Syncthing).
// Contract with the shell (qml/shell/ShellRouteView.qml): `route`, optional `title`.
FocusScope {
    id: root

    property var route: ({})
    readonly property int maximumWidth: 860
    readonly property real sideMargin: Math.max(Theme.compact ? Theme.space3 : Theme.space6, (width - maximumWidth) / 2)
    property var items: []

    function reload() {
        items = Workspace.trashItems()
    }
    function sizeLabel(bytes) {
        const locale = Qt.locale(DatesHelper.locale === "en" ? "en_US" : "pl_PL")
        if (bytes < 1024)
            return qsTr("%1 B").arg(bytes)
        if (bytes < 1024 * 1024)
            return qsTr("%1 kB").arg(Number(bytes / 1024).toLocaleString(locale, "f", bytes < 10240 ? 1 : 0))
        return qsTr("%1 MB").arg(Number(bytes / (1024 * 1024)).toLocaleString(locale, "f", 1))
    }
    function restore(item) {
        const path = Workspace.restore(item.trashPath)
        if (path === "") {
            AppController.toast(qsTr("“%1” could not be restored.").arg(item.title), "error")
            return
        }
        const folder = Workspace.folderOf(path)
        AppController.toast(folder !== "" ? qsTr("“%1” is back in the folder %2").arg(item.title).arg(folder)
                                          : qsTr("“%1” is back in the notebook").arg(item.title), "success")
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
                    visible: root.items.length > 0
                    variant: "danger"
                    iconName: "trash-2"
                    text: qsTr("Empty trash")
                    onClicked: emptyDialog.open()
                }
            }
        }

        delegate: Item {
            id: entry
            required property int index
            required property var modelData
            readonly property var item: modelData
            readonly property string journalDay: item.folder ? "" : Workspace.journalDate(item.original)
            readonly property string shownTitle: journalDay !== "" ? DatesHelper.formatLong(journalDay) : item.title

            width: list.width
            implicitHeight: Math.max(Theme.listItemHeightTwoLine, rowLayout.implicitHeight + 2 * Theme.space2)

            Rectangle {
                x: root.sideMargin - Theme.space2
                width: list.width - 2 * root.sideMargin + 2 * Theme.space2
                height: parent.height
                radius: Theme.radius
                color: entryHover.hovered ? Theme.bgHover : "transparent"
                HoverHandler { id: entryHover; enabled: !Theme.touchUi }
            }

            RowLayout {
                id: rowLayout
                x: root.sideMargin
                width: list.width - 2 * root.sideMargin
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.space3

                BIcon {
                    name: entry.item.folder ? "folder" : entry.journalDay !== "" ? "calendar-days" : "file-text"
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
                        text: [entry.journalDay !== "" ? qsTr("journal")
                               : Workspace.folderOf(entry.item.original) !== "" ? qsTr("from %1").arg(Workspace.folderOf(entry.item.original)) : "",
                               entry.item.when !== "" ? qsTr("deleted %1").arg(entry.item.when) : "",
                               entry.item.folder ? qsTr("%n file(s)", "", entry.item.files) : "",
                               root.sizeLabel(entry.item.size)].filter(s => s !== "").join(" · ")
                        variant: "small"
                        color: Theme.fgMuted
                        wrapMode: Text.Wrap
                    }
                }
                BButton {
                    size: "small"
                    iconName: "undo-2"
                    text: Theme.compact ? "" : qsTr("Restore")
                    Accessible.name: qsTr("Restore %1").arg(entry.shownTitle)
                    onClicked: root.restore(entry.item)
                }
                BIconButton {
                    size: "small"
                    variant: "ghost"
                    iconName: "x"
                    iconColor: Theme.dangerText
                    label: qsTr("Delete %1 for good").arg(entry.shownTitle)
                    onClicked: deleteDialog.openFor(entry.item)
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

    BConfirmDialog {
        id: deleteDialog
        property var item: ({})
        function openFor(i) {
            item = i
            open()
        }
        parent: T.Overlay.overlay
        title: qsTr("Delete for good?")
        message: deleteDialog.item.folder
                 ? qsTr("The folder “%1” and the %n file(s) in it will be deleted permanently.", "", deleteDialog.item.files || 0).arg(deleteDialog.item.title)
                 : qsTr("“%1” will be deleted permanently. This cannot be undone.").arg(deleteDialog.item.title)
        confirmText: qsTr("Delete")
        destructive: true
        onAccepted: {
            if (Workspace.deleteForever(deleteDialog.item.trashPath))
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
