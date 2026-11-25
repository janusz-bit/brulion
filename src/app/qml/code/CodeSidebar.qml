pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// CodeSidebar — Code mode's panel beside the rail (docs/CODE.md, docs/UI.md "Code mode"): the
// projects of this device (Workspace.projects), each with its threads (ChatListModel of the
// project: a thread is an AI chat conversation whose page names it) — what needs the user first
// (asking, failed, unread), a spinner while an agent works. A project's row folds its threads
// (kept per device: "code/collapsed"); its "+" starts a new thread there; its menu renames it,
// opens its folder in the editor or the file manager, sets its script buttons and takes it off
// the list (its folder and threads stay). A thread's row opens it (route {view: "code", path}),
// its menu archives or deletes it. "Add a project…" picks a folder. AppShell shows it in place of
// ShellSidebar while AppSettings.appMode is "code"; on phones it is the drawer's content.
Rectangle {
    id: sidebar

    property real topInset: 0
    property real bottomInset: 0
    /// Room at the header's start for AppShell's panel button drawn over it.
    property real leadingRoom: 0

    signal navigated()

    readonly property string routePath: AppController.currentView === "code"
                                        ? AppController.routeText(AppController.currentRoute, "path") : ""
    readonly property string routeProject: AppController.routeText(AppController.currentRoute, "project")
    readonly property bool routeNew: AppController.currentView === "code" && AppController.routeFlag(AppController.currentRoute, "new")
    /// The folded projects' ids (this device's).
    property list<string> collapsed: Js.nonEmptyStrings(Js.split("" + AppSettings.value("code/collapsed", ""), ","))

    function go(route: var): void {
        AppController.navigate(route)
        sidebar.navigated()
    }
    function folded(id: string): bool {
        return sidebar.collapsed.indexOf(id) >= 0
    }
    function toggle(id: string): void {
        sidebar.collapsed = sidebar.folded(id) ? Js.withoutString(sidebar.collapsed, id)
                                               : Js.appendedString(sidebar.collapsed, id)
        AppSettings.setValue("code/collapsed", Js.joinNonEmpty(sidebar.collapsed, ","))
    }
    function addProject(folder: url): void {
        const id = Workspace.projects.add("" + folder)
        if (id === "") {
            AppController.toast(qsTr("That folder could not be added."), "error")
            return
        }
        sidebar.go({ view: "code", new: true, project: id })
    }

    color: Theme.bgSunken
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Projects")

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: sidebar.topInset
        anchors.bottomMargin: sidebar.bottomInset
        spacing: 0

        // ---- Header: the mode's name, add a project ----------------------------------------
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.touchUi ? 56 : 46
            Layout.leftMargin: Math.max(Theme.space3, sidebar.leadingRoom)
            Layout.rightMargin: Theme.space2
            spacing: Theme.space1
            BText {
                Layout.fillWidth: true
                text: qsTr("Projects")
                font.weight: Theme.weightSemiBold
                elide: Text.ElideRight
            }
            BIconButton {
                objectName: "codeAddProject"
                size: "small"
                iconName: "folder-plus"
                label: qsTr("Add a project…")
                onClicked: folderPicker.open()
            }
        }

        Flickable {
            id: flick
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentHeight: projectsColumn.implicitHeight + Theme.space4
            boundsBehavior: Flickable.StopAtBounds
            TouchFlick.enabled: true
            T.ScrollBar.vertical: BScrollBar {}

            ColumnLayout {
                id: projectsColumn
                width: flick.width
                spacing: Theme.space1

                Repeater {
                    model: Workspace.projects.list
                    delegate: ProjectSection {}
                }

                // No project yet: what Code mode is, and the way in.
                ColumnLayout {
                    objectName: "codeNoProjects"
                    visible: Workspace.projects.count === 0
                    Layout.fillWidth: true
                    Layout.margins: Theme.space4
                    spacing: Theme.space3
                    BText {
                        Layout.fillWidth: true
                        text: qsTr("Add the folder of a project: AI agents such as Claude Code or Codex work in it, each thread on its own, as in T3 Code.")
                        variant: "small"
                        color: Theme.fgMuted
                        wrapMode: Text.Wrap
                    }
                    BButton {
                        objectName: "codeAddProjectEmpty"
                        size: "small"
                        variant: "secondary"
                        iconName: "folder-plus"
                        text: qsTr("Add a project…")
                        onClicked: folderPicker.open()
                    }
                }
            }
        }
    }

    // ---- A project with its threads ----------------------------------------------------------
    component ProjectSection: ColumnLayout {
        id: section
        required property var modelData
        readonly property string projectId: AppController.routeText(section.modelData, "id")
        readonly property string projectName: AppController.routeText(section.modelData, "name")
        readonly property string projectPath: AppController.routeText(section.modelData, "path")
        readonly property bool open: !sidebar.folded(section.projectId)

        Layout.fillWidth: true
        spacing: 0

        ChatListModel {
            id: threads
            workspace: Workspace
            locale: DatesHelper.locale
            project: section.projectId
        }

        Item {
            id: projectRow
            objectName: "codeProject_" + section.projectId
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.listItemHeight
            Accessible.role: Accessible.Button
            Accessible.name: section.projectName

            Rectangle {
                anchors.fill: parent
                anchors.leftMargin: Theme.space1
                anchors.rightMargin: Theme.space1
                radius: Theme.radius
                color: projectHover.hovered ? Theme.bgHover : "transparent"
            }
            HoverHandler { id: projectHover; enabled: !Theme.touchUi }
            TapHandler {
                acceptedButtons: Qt.LeftButton
                onTapped: sidebar.toggle(section.projectId)
                onLongPressed: projectMenu.popup(projectRow, projectRow.width / 2, projectRow.height)
            }
            TapHandler {
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                acceptedButtons: Qt.RightButton
                onTapped: (point) => projectMenu.popup(projectRow, point.position.x, point.position.y)
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.space3
                anchors.rightMargin: Theme.space2
                spacing: Theme.space2
                BIcon {
                    name: section.open ? "chevron-down" : "chevron-right"
                    size: Theme.iconSizeSm
                    color: Theme.fgFaint
                }
                BIcon {
                    name: "folder"
                    size: Theme.iconSize
                    color: Theme.fgMuted
                }
                BText {
                    Layout.fillWidth: true
                    text: section.projectName
                    font.weight: Theme.weightSemiBold
                    elide: Text.ElideRight
                }
                BIconButton {
                    objectName: "codeNewThread_" + section.projectId
                    size: "small"
                    iconName: "plus"
                    label: qsTr("New thread in %1").arg(section.projectName)
                    onClicked: sidebar.go({ view: "code", new: true, project: section.projectId })
                }
                BIconButton {
                    id: projectMore
                    objectName: "codeProjectMore_" + section.projectId
                    size: "small"
                    iconName: "ellipsis"
                    label: qsTr("More: %1").arg(section.projectName)
                    onClicked: projectMenu.popup(projectMore, 0, projectMore.height)
                }
            }
            BMenu {
                id: projectMenu
                objectName: "codeProjectMenu"
                BMenuItem {
                    objectName: "codeProjectNewThread"
                    text: qsTr("New thread")
                    iconName: "plus"
                    onTriggered: sidebar.go({ view: "code", new: true, project: section.projectId })
                }
                BMenuItem {
                    objectName: "codeProjectEditor"
                    text: qsTr("Open in editor")
                    iconName: "code"
                    onTriggered: {
                        if (!Workspace.projects.openInEditor(section.projectPath))
                            AppController.toast(qsTr("The editor could not be started: set its command in Settings → Extensions → Code mode."), "error")
                    }
                }
                BMenuItem {
                    objectName: "codeProjectFolder"
                    text: qsTr("Show the folder")
                    iconName: "folder-open"
                    onTriggered: Workspace.projects.openFolder(section.projectPath)
                }
                BMenuItem {
                    objectName: "codeProjectSettings"
                    text: qsTr("Name and scripts…")
                    iconName: "sliders-horizontal"
                    onTriggered: projectDialog.openFor(section.projectId)
                }
                BMenuSeparator {}
                BMenuItem {
                    objectName: "codeProjectRemove"
                    text: qsTr("Remove from the list")
                    iconName: "x"
                    danger: true
                    onTriggered: removeDialog.openFor(section.projectId, section.projectName)
                }
            }
        }

        Repeater {
            model: section.open ? threads : null
            delegate: ThreadRow {}
        }
        BText {
            visible: section.open && threads.count === 0
            Layout.fillWidth: true
            Layout.leftMargin: Theme.space8 + Theme.space2
            Layout.bottomMargin: Theme.space1
            text: qsTr("No threads yet")
            variant: "small"
            color: Theme.fgFaint
        }
    }

    // ---- A thread ----------------------------------------------------------------------------
    component ThreadRow: Item {
        id: thread
        required property string path
        required property string title
        required property string dateLabel
        required property bool running
        required property bool queued
        required property bool asking
        required property string failure
        required property bool unread
        required property bool archived
        readonly property bool current: thread.path === sidebar.routePath

        objectName: "codeThread"
        Layout.fillWidth: true
        Layout.preferredHeight: Theme.listItemHeight
        Accessible.role: Accessible.Button
        Accessible.name: thread.title
        Accessible.description: thread.asking ? qsTr("asks you something") : thread.failure !== "" ? qsTr("failed")
                                : thread.running ? qsTr("working") : thread.unread ? qsTr("unread") : ""

        Rectangle {
            anchors.fill: parent
            anchors.leftMargin: Theme.space1
            anchors.rightMargin: Theme.space1
            radius: Theme.radius
            color: thread.current ? Theme.bgSelected : threadHover.hovered ? Theme.bgHover : "transparent"
        }
        HoverHandler { id: threadHover; enabled: !Theme.touchUi }
        TapHandler {
            acceptedButtons: Qt.LeftButton
            onTapped: sidebar.go({ view: "code", path: thread.path })
            onLongPressed: threadMenu.popup(thread, thread.width / 2, thread.height)
        }
        TapHandler {
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            acceptedButtons: Qt.RightButton
            onTapped: (point) => threadMenu.popup(thread, point.position.x, point.position.y)
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.space8
            anchors.rightMargin: Theme.space3
            spacing: Theme.space2
            // Its state: working, asking, failed, unread — else nothing.
            Item {
                Layout.preferredWidth: Theme.iconSizeSm
                Layout.preferredHeight: Theme.iconSizeSm
                BSpinner {
                    anchors.centerIn: parent
                    visible: thread.running && !thread.asking
                    size: Theme.iconSizeSm
                    calm: true
                }
                BIcon {
                    anchors.centerIn: parent
                    visible: thread.asking || (!thread.running && thread.failure !== "")
                    name: thread.asking ? "circle-help" : "circle-alert"
                    size: Theme.iconSizeSm
                    color: thread.asking ? Theme.accent : Theme.danger
                }
                Rectangle {
                    anchors.centerIn: parent
                    visible: thread.unread && !thread.running && !thread.asking && thread.failure === ""
                    width: 8
                    height: 8
                    radius: 4
                    color: Theme.accent
                }
            }
            BText {
                Layout.fillWidth: true
                text: thread.title
                color: thread.archived ? Theme.fgMuted : Theme.fg
                font.weight: thread.unread || thread.current ? Theme.weightSemiBold : Theme.weightNormal
                elide: Text.ElideRight
            }
            BText {
                text: thread.dateLabel
                variant: "small"
                color: Theme.fgFaint
            }
        }
        BMenu {
            id: threadMenu
            objectName: "codeThreadMenu"
            BMenuItem {
                text: thread.archived ? qsTr("Unarchive") : qsTr("Archive")
                iconName: "inbox"
                onTriggered: Workspace.chats.setArchived(thread.path, !thread.archived)
            }
            BMenuSeparator {}
            BMenuItem {
                objectName: "codeThreadDelete"
                text: qsTr("Move to trash")
                iconName: "trash-2"
                danger: true
                onTriggered: {
                    if (Workspace.chats.remove(thread.path) === "")
                        return
                    AppController.toastRestore(qsTr("Thread moved to trash"), qsTr("Undo"), Workspace.chats.lastRemoved())
                    if (thread.current)
                        AppController.replaceRoute({ view: "code" })
                }
            }
        }
    }

    FilePicker {
        id: folderPicker
        objectName: "codeProjectFolderDialog"
        title: qsTr("Choose the folder of a project")
        fileMode: FilePicker.OpenFolder
        dark: Theme.dark
        colors: Theme.colors
        onAccepted: sidebar.addProject(folderPicker.selectedFolder)
    }

    CodeProjectDialog {
        id: projectDialog
        parent: T.Overlay.overlay
    }

    BConfirmDialog {
        id: removeDialog
        property string projectId
        function openFor(id: string, name: string): void {
            removeDialog.projectId = id
            removeDialog.subject = name
            removeDialog.open()
        }
        parent: T.Overlay.overlay
        title: qsTr("Remove the project from the list?")
        subjectIcon: "folder"
        message: qsTr("Its folder, its files and its threads stay as they are; add the folder again to see them here.")
        confirmText: qsTr("Remove")
        onAccepted: Workspace.projects.remove(removeDialog.projectId)
    }
}
