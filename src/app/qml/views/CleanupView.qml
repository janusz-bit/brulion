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
    property string kind: AppController.routeText(route, "kind") !== "" ? AppController.routeText(route, "kind") : "all" // "all" | "notes" | "attachments"
    /// The ticked paths (replaced, not changed, so that bindings follow).
    property list<string> ticked: selection.routeTicks(route)
    readonly property list<var> files: selection.files
    /// The rows of the kind shown, and the paths of those ticked (the shell's self-test reads them).
    readonly property list<var> shown: selection.shown
    readonly property list<string> shownTicked: selection.shownTicked

    function reload(): void {
        selection.files = Workspace.unreferencedFiles()
        if (loading)
            return // no list yet: the ticks of the route wait for it
        ticked = selection.existingTicks()
    }
    function setTicked(path: string, on: bool): void {
        ticked = selection.withTicked(path, on)
    }
    function tickAllShown(on: bool): void {
        ticked = selection.withAllShown(on)
    }
    function open(path: string): void {
        AppController.replaceRoute({ view: "cleanup", kind: kind, selected: ticked })
        AppController.navigate({ view: "page", path: path })
    }
    function moveTickedToTrash(): void {
        const trashed = root.trashed(Workspace.deletePages(selection.shownTicked))
        if (trashed.length > 0) {
            // Restored by the controller, not by this view: the toast outlives the view when the
            // user goes elsewhere before pressing Cofnij.
            AppController.toastRestore(qsTr("%n file(s) moved to the trash", "", trashed.length), qsTr("Undo"), trashed)
        }
        reload()
    }
    function trashed(paths: list<string>): list<string> {
        return paths
    }
    function nonEmpty(parts: list<string>): list<string> {
        return Js.nonEmptyStrings(parts)
    }
    function subtitle(openTasks: int, reminders: int, folder: string, sizeText: string, modified: string): string {
        return nonEmpty([openTasks > 0 ? qsTr("%n open task(s)", "", openTasks) : "",
                         reminders > 0 ? qsTr("%n reminder(s)", "", reminders) : "",
                         folder, sizeText, modified]).join(" · ")
    }

    CleanupSelection {
        id: selection
        kind: root.kind
        ticked: root.ticked
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
                    { value: "notes", text: qsTr("Notes (%1)").arg(root.files.length - selection.attachmentCount) },
                    { value: "attachments", text: qsTr("Attachments (%1)").arg(selection.attachmentCount) }]
            currentValue: root.kind
            onActivated: (value) => root.kind = value
        }

        Flow {
            visible: selection.shown.length > 0
            Layout.fillWidth: true
            spacing: Theme.space3

            BCheckBox {
                objectName: "cleanupTickAll"
                text: selection.shownTicked.length > 0 ? qsTr("%n selected", "", selection.shownTicked.length) : qsTr("Select all")
                tristate: true
                // Not onToggled: from some ticked to all, `checked` stays true. A click ticks all
                // unless all is ticked; the binding below then shows it.
                onClicked: root.tickAllShown(selection.shownTicked.length !== selection.shown.length)
                Binding on checkState {
                    value: selection.shownTicked.length === 0 ? Qt.Unchecked
                           : selection.shownTicked.length === selection.shown.length ? Qt.Checked : Qt.PartiallyChecked
                }
            }
            BButton {
                objectName: "cleanupTrash"
                variant: "danger"
                iconName: "trash-2"
                enabled: selection.shownTicked.length > 0
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
            model: selection.shown
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
                required property string path
                required property string title
                required property string folder
                required property bool attachment
                required property bool picture
                required property bool pdf
                required property string sizeText
                required property string modified
                required property int openTasks
                required property int reminders
                readonly property bool tick: root.ticked.indexOf(path) >= 0

                width: list.width
                spacing: 0

                BCheckBox {
                    id: box
                    Layout.leftMargin: Theme.space2
                    Accessible.name: qsTr("Select %1").arg(entry.title)
                    onToggled: root.setTicked(entry.path, checked)
                    Binding on checked { value: entry.tick }
                }
                BListItem {
                    Layout.fillWidth: true
                    iconName: entry.picture ? "image" : entry.pdf ? "file" : entry.attachment ? "paperclip" : "file-text"
                    text: entry.title
                    subtitle: root.subtitle(entry.openTasks, entry.reminders, entry.folder, entry.sizeText, entry.modified)
                    selected: entry.tick
                    onClicked: root.setTicked(entry.path, !entry.tick)
                    trailing: BIconButton {
                        size: "small"
                        variant: "ghost"
                        iconName: "external-link"
                        label: qsTr("Open %1").arg(entry.title)
                        onClicked: root.open(entry.path)
                    }
                }
            }

            BEmptyState {
                parent: list
                anchors.centerIn: parent
                width: Math.min(parent.width - 2 * Theme.space6, implicitWidth)
                visible: !root.loading && selection.shown.length === 0
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
        message: qsTr("%n file(s) will be moved to the trash. You can restore them from there.", "", selection.shownTicked.length)
        detail: selection.tickedWithWork > 0
                ? qsTr("Files with open tasks or reminders among them: %1. Those tasks and reminders will stop showing.").arg(selection.tickedWithWork) : ""
        confirmText: qsTr("Move to trash")
        destructive: true
        onAccepted: root.moveTickedToTrash()
    }
}
