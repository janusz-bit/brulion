pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// CleanupView — route "cleanup" (docs/UI.md "Cleaning up"): the notes and attachments nothing refers
// to (Workspace.unreferencedFiles). Nothing is ticked at first: the user picks what goes, by kind
// (BSegmented), one by one or all shown, and only what is ticked and shown goes to the trash after
// a confirmation, with Cofnij in the toast. Removing a note can leave its picture without links:
// the list follows the index, so that picture appears here next.
// A file is opened with the row's button; the kind and the ticks stay in the route for Back.
FocusScope {
    id: root

    property var route: ({})
    readonly property int maximumWidth: 860
    readonly property real sideMargin: Math.max(Theme.compact ? Theme.space3 : Theme.space6, (width - maximumWidth) / 2)
    /// The notebook's links are not known yet (a notebook being opened): nothing to offer.
    readonly property bool loading: !Workspace.ready
    property var files: []
    property string kind: route.kind || "all" // "all" | "notes" | "attachments"
    /// path -> true (replaced, not changed, so that bindings follow).
    property var ticked: {
        const out = {}
        for (const path of route.selected || [])
            out[path] = true
        return out
    }

    readonly property var shown: kind === "all" ? files
                                 : files.filter(f => f.attachment === (kind === "attachments"))
    readonly property var shownTicked: shown.filter(f => ticked[f.path])
    readonly property int attachmentCount: files.filter(f => f.attachment).length
    /// Of what is about to go: the files holding something still to do.
    readonly property int tickedWithWork: shownTicked.filter(f => f.openTasks > 0 || f.reminders > 0).length

    function reload() {
        files = Workspace.unreferencedFiles()
        if (loading)
            return // no list yet: the ticks of the route wait for it
        const left = {}
        for (const file of files) {
            if (ticked[file.path])
                left[file.path] = true
        }
        ticked = left
    }
    function setTicked(path, on) {
        const next = Object.assign({}, ticked)
        if (on)
            next[path] = true
        else
            delete next[path]
        ticked = next
    }
    function tickAllShown(on) {
        const next = Object.assign({}, ticked)
        for (const file of shown) {
            if (on)
                next[file.path] = true
            else
                delete next[file.path]
        }
        ticked = next
    }
    function open(path) {
        AppController.replaceRoute({ view: "cleanup", kind: kind, selected: Object.keys(ticked) })
        AppController.navigate({ view: "page", path: path })
    }
    function moveTickedToTrash() {
        const trashed = Workspace.deletePages(shownTicked.map(f => f.path))
        if (trashed.length > 0) {
            // A bound method, not a function of this view: the toast outlives the view when the
            // user goes elsewhere before pressing Cofnij.
            AppController.toastAction(qsTr("%n file(s) moved to the trash", "", trashed.length), "info", qsTr("Undo"),
                                      Workspace.restoreAll.bind(Workspace, trashed))
        }
        reload()
    }
    function subtitle(file) {
        return [file.openTasks > 0 ? qsTr("%n open task(s)", "", file.openTasks) : "",
                file.reminders > 0 ? qsTr("%n reminder(s)", "", file.reminders) : "",
                file.folder,
                file.sizeText,
                file.modified].filter(s => s !== "").join(" · ")
    }

    focus: true
    Component.onCompleted: reload()

    // An index change may come with every saved note: the list is computed once they stop.
    Timer {
        id: reloadTimer
        interval: 300
        onTriggered: root.reload()
    }
    Connections {
        target: Workspace
        function onIndexChanged() { reloadTimer.restart() }
        function onFavoritesChanged() { reloadTimer.restart() }
        function onOpenChanged() { reloadTimer.restart() }
        function onReadyChanged() { root.reload() }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.bg
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: root.sideMargin
        anchors.rightMargin: root.sideMargin
        anchors.topMargin: Theme.compact ? Theme.space3 : Theme.space6
        spacing: Theme.space3

        BText {
            Layout.fillWidth: true
            text: qsTr("Notes and attachments that no link, tag or query leads to. Daily notes, templates and favourites are left out. Tick what you no longer need: it goes to the trash, where you can restore it.")
            variant: "muted"
            wrapMode: Text.Wrap
        }

        BSegmented {
            objectName: "cleanupKind"
            visible: root.files.length > 0
            Layout.fillWidth: Theme.compact
            fill: Theme.compact
            accessibleName: qsTr("Show")
            model: [{ value: "all", text: qsTr("All (%1)").arg(root.files.length) },
                    { value: "notes", text: qsTr("Notes (%1)").arg(root.files.length - root.attachmentCount) },
                    { value: "attachments", text: qsTr("Attachments (%1)").arg(root.attachmentCount) }]
            currentValue: root.kind
            onActivated: (value) => root.kind = value
        }

        Flow {
            visible: root.shown.length > 0
            Layout.fillWidth: true
            spacing: Theme.space3

            BCheckBox {
                objectName: "cleanupTickAll"
                text: root.shownTicked.length > 0 ? qsTr("%n selected", "", root.shownTicked.length) : qsTr("Select all")
                tristate: true
                nextCheckState: () => root.shownTicked.length === root.shown.length ? Qt.Unchecked : Qt.Checked
                // Not onToggled: from some ticked to all, `checked` stays true.
                onClicked: root.tickAllShown(root.shownTicked.length !== root.shown.length)
                Binding on checkState {
                    value: root.shownTicked.length === 0 ? Qt.Unchecked
                           : root.shownTicked.length === root.shown.length ? Qt.Checked : Qt.PartiallyChecked
                }
            }
            BButton {
                objectName: "cleanupTrash"
                variant: "danger"
                iconName: "trash-2"
                enabled: root.shownTicked.length > 0
                text: qsTr("Move to trash")
                onClicked: confirmDialog.open()
            }
        }

        ListView {
            id: list
            objectName: "cleanupList"
            TouchFlick.enabled: true
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: -Theme.space2
            Layout.rightMargin: -Theme.space2
            clip: true
            focus: true
            model: root.shown
            currentIndex: -1
            keyNavigationEnabled: true
            boundsBehavior: Flickable.StopAtBounds
            bottomMargin: Theme.narrow ? Theme.space16 + Theme.space8 : Theme.space8
            T.ScrollBar.vertical: BScrollBar {}
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Notes and attachments without links")

            delegate: RowLayout {
                id: entry
                required property int index
                required property var modelData
                readonly property var file: modelData
                readonly property bool tick: !!root.ticked[file.path]

                width: list.width
                spacing: 0

                BCheckBox {
                    id: box
                    Layout.leftMargin: Theme.space2
                    Accessible.name: qsTr("Select %1").arg(entry.file.title)
                    onToggled: root.setTicked(entry.file.path, checked)
                    Binding on checked { value: entry.tick }
                }
                BListItem {
                    Layout.fillWidth: true
                    iconName: entry.file.picture ? "image" : entry.file.pdf ? "file" : entry.file.attachment ? "paperclip" : "file-text"
                    text: entry.file.title
                    subtitle: root.subtitle(entry.file)
                    selected: entry.tick
                    onClicked: root.setTicked(entry.file.path, !entry.tick)
                    trailing: BIconButton {
                        size: "small"
                        variant: "ghost"
                        iconName: "external-link"
                        label: qsTr("Open %1").arg(entry.file.title)
                        onClicked: root.open(entry.file.path)
                    }
                }
            }

            BEmptyState {
                parent: list
                anchors.centerIn: parent
                width: Math.min(parent.width - 2 * Theme.space6, implicitWidth)
                visible: !root.loading && root.shown.length === 0
                iconName: "sparkles"
                title: qsTr("Nothing to clean up")
                description: qsTr("There are no notes or attachments without links here.")
            }
            BSpinner {
                parent: list
                anchors.centerIn: parent
                visible: root.loading
            }
        }
    }

    BConfirmDialog {
        id: confirmDialog
        objectName: "cleanupConfirm"
        parent: T.Overlay.overlay
        title: qsTr("Move to trash?")
        message: qsTr("%n file(s) will be moved to the trash. You can restore them from there.", "", root.shownTicked.length)
        detail: root.tickedWithWork > 0
                ? qsTr("Files with open tasks or reminders among them: %1. Those tasks and reminders will stop showing.").arg(root.tickedWithWork) : ""
        confirmText: qsTr("Move to trash")
        destructive: true
        onAccepted: root.moveTickedToTrash()
    }
}
