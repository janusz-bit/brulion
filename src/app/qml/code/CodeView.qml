pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic as C
import QtQuick.Layouts
import Brulion

// CodeView — route "code", Code mode's main view (docs/CODE.md, docs/UI.md "Code mode"), as T3 Code:
// an AI agent's thread of a project — the conversation (ChatView as a thread: routeView "code", no
// list; CodeSidebar lists the threads) with, beside it, the Changes panel (GitChangesPanel: the
// project's working tree against git, a turn's changes, commit / push / pull request) and under it
// the Terminal panel (TerminalPanel: the thread's terminals — TerminalRegistry keeps them, running,
// while another thread is shown —; the project's script buttons run there).
//
//   {view: "code"}                          a new thread of the last project used (or the first)
//   {view: "code", new: true, project}      a new thread of `project`
//   {view: "code", path: "AI/….md"}         a thread
//   {view: "code", addProject: true}        … and the folder picker (the palette's "Add a project…")
//
// The header: the project (for a new thread a menu of them), where the thread works — the
// project's folder or its own git worktree and branch (a new thread chooses: Local / New worktree,
// CodeProjects.createWorktree before its first message) —, the script buttons, Open in editor and
// the panels' toggles (kept per device: "code/changesOpen", "code/terminalOpen").
FocusScope {
    id: root

    property var route: ({})
    readonly property string path: AppController.routeText(root.route, "path")
    readonly property string routeProject: AppController.routeText(root.route, "project")
    /// Where the thread works ({project, projectName, worktree, branch, folder, known, exists}).
    property var place: ({})
    readonly property string lastProject: "" + AppSettings.value("code/lastProject", "")
    /// The thread's project, else the new thread's: the route's, the last one used, the first.
    readonly property string project: root.path !== "" ? AppController.routeText(root.place, "project")
                                      : root.routeProject !== "" && Workspace.projects.contains(root.routeProject) ? root.routeProject
                                      : root.lastProject !== "" && Workspace.projects.contains(root.lastProject) ? root.lastProject
                                      : root.firstProject()
    readonly property string projectName: root.path !== "" ? AppController.routeText(root.place, "projectName")
                                          : Workspace.projects.nameOf(root.project)
    readonly property string folder: root.path !== "" ? AppController.routeText(root.place, "folder")
                                     : Workspace.projects.folderOf(root.project)
    readonly property string branch: AppController.routeText(root.place, "branch")
    readonly property bool worktree: AppController.routeText(root.place, "worktree") !== ""
    readonly property list<var> scripts: AppController.routeList(Workspace.projects.project(root.project), "scripts")
    /// A new thread gets a git worktree of its own (this device's choice, kept).
    property bool useWorktree: AppSettings.value("code/newWorktree", false) === true
    property bool changesOpen: AppSettings.value("code/changesOpen", true) !== false
    property bool terminalOpen: AppSettings.value("code/terminalOpen", false) === true
    /// A worktree is being made for the new thread's first message.
    property bool makingWorktree: false
    readonly property string title: root.path !== "" ? chat.title : root.projectName !== "" ? qsTr("New thread") : qsTr("Code")
    readonly property bool wide: root.width >= 900

    function firstProject(): string {
        const all = Workspace.projects.list
        return all.length > 0 ? AppController.routeText(all[0], "id") : ""
    }
    function refreshPlace(): void {
        root.place = root.path !== "" ? Workspace.chats.threadPlace(root.path) : root.noPlace
    }
    readonly property var noPlace: ({})
    function setChangesOpen(on: bool): void {
        root.changesOpen = on
        AppSettings.setValue("code/changesOpen", on)
    }
    function setTerminalOpen(on: bool): void {
        root.terminalOpen = on
        AppSettings.setValue("code/terminalOpen", on)
    }
    function runScript(command: string, name: string): void {
        root.setTerminalOpen(true)
        root.pendingScript = command
        root.pendingScriptName = name
        root.runPending()
    }
    /// The script asked for, run once the thread's terminal panel is there.
    property string pendingScript
    property string pendingScriptName
    function runPending(): void {
        const panel = terminalLoader.item as TerminalPanel
        if (root.pendingScript === "" || panel === null || panel.threadKey !== root.terminalKey)
            return
        panel.run(root.pendingScript, root.pendingScriptName)
        root.pendingScript = ""
    }
    /// Whose terminals the panel shows (TerminalRegistry: a thread's stay, running, while another
    /// thread is shown): the thread's page, a new thread's folder until its first message.
    readonly property string terminalKey: root.path !== "" ? root.path : root.folder
    /// The panel is made the first time it is shown, then kept.
    property bool terminalWanted: false
    onTerminalOpenChanged: root.followFolder()
    function followFolder(): void {
        if (root.terminalOpen)
            root.terminalWanted = true
    }
    /// A terminal command of the palette or its shortcut (CommandModel's "terminal.*", uiAction
    /// "terminal:…"): the panel shown (made) first; `terminal:toggle` shows or hides it.
    property string pendingTerminalAction
    function terminalAction(action: string): void {
        if (!Js.startsWith(action, "terminal:") || !root.visible)
            return
        if (action === "terminal:toggle" && root.terminalOpen) {
            root.setTerminalOpen(false)
            chat.forceActiveFocus()
            return
        }
        root.setTerminalOpen(true)
        root.pendingTerminalAction = action
        root.runTerminalAction()
    }
    function runTerminalAction(): void {
        const panel = terminalLoader.item as TerminalPanel
        const action = root.pendingTerminalAction
        if (action === "" || panel === null)
            return
        root.pendingTerminalAction = ""
        switch (action) {
        case "terminal:new": panel.newTerminal(); break
        case "terminal:splitRight": panel.split(Qt.Horizontal); break
        case "terminal:splitDown": panel.split(Qt.Vertical); break
        case "terminal:close": panel.closeCurrent(); break
        case "terminal:next": panel.focusNext(1); break
        case "terminal:previous": panel.focusNext(-1); break
        default: panel.focusTerminal(); break
        }
    }
    Connections {
        target: AppController
        function onUiActionRequested(action: string) { root.terminalAction(action) }
    }
    Connections {
        target: chat
        // A new thread's first message made its page (the route follows a moment later): its
        // terminals — keyed by its folder until now — are the thread's.
        function onPathChanged() {
            if (root.path === "" && chat.path !== "" && root.folder !== "")
                TerminalRegistry.rekey(root.folder, chat.path)
        }
    }

    onPathChanged: root.refreshPlace()
    onProjectChanged: {
        if (root.path === "" && root.project !== "")
            AppSettings.setValue("code/lastProject", root.project)
    }
    onRouteChanged: {
        root.refreshPlace()
        chat.newWorktree = ""
        chat.newBranch = ""
        if (AppController.routeFlag(root.route, "addProject"))
            folderPicker.open()
    }
    Component.onCompleted: {
        root.refreshPlace()
        root.followFolder()
        if (AppController.routeFlag(root.route, "addProject"))
            folderPicker.open()
    }
    Connections {
        target: Workspace.chats
        // (A thread's worktree set or its page changed: where it works again; its answer written:
        // the changes again.)
        function onWritten(path: string) {
            if (path !== root.path)
                return
            root.refreshPlace()
            changes.git.refresh()
        }
    }
    Connections {
        target: Workspace
        // (Shown before the notebook was ready: where the thread works once it is.)
        function onReadyChanged() { root.refreshPlace() }
    }
    Connections {
        target: Workspace.projects
        function onChanged() { root.refreshPlace() }
        function onWorktreeCreated(project: string, folder: string, branch: string, error: string) {
            if (!root.makingWorktree || project !== root.project)
                return
            root.makingWorktree = false
            if (error !== "") {
                AppController.toast(qsTr("The worktree could not be made: %1").arg(error), "error")
                return
            }
            chat.newWorktree = folder
            chat.newBranch = branch
            chat.send()
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.bg
    }

    // ---- No project yet ------------------------------------------------------------------------
    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * Theme.space6, 480)
        visible: Workspace.projects.count === 0
        spacing: Theme.space4
        BEmptyState {
            objectName: "codeEmpty"
            Layout.alignment: Qt.AlignHCenter
            iconName: "code"
            title: qsTr("Code mode")
            description: qsTr("Add the folder of a project. AI agents of yours — Claude Code, Codex, OpenCode — work in it with your subscription, each thread on its own (in a git worktree if you like); you see their changes, run the project's scripts in a terminal, commit and open pull requests here. Brulion notifies you when they answer or need you.")
        }
        BButton {
            objectName: "codeEmptyAddProject"
            Layout.alignment: Qt.AlignHCenter
            iconName: "folder-plus"
            text: qsTr("Add a project…")
            onClicked: folderPicker.open()
        }
        BButton {
            Layout.alignment: Qt.AlignHCenter
            variant: "ghost"
            visible: !Workspace.chats.agentsAvailable || AppSettings.acpAgents.length === 0
            iconName: "sparkles"
            text: qsTr("Set up an AI agent")
            onClicked: AppController.navigate({ view: "settings", section: "extensions" })
        }
    }

    ColumnLayout {
        anchors.fill: parent
        visible: Workspace.projects.count > 0
        spacing: 0

        // ---- The thread's header: project, where it works, scripts, editor, panels --------------
        RowLayout {
            id: header
            objectName: "codeHeader"
            Layout.fillWidth: true
            Layout.leftMargin: Theme.space3
            Layout.rightMargin: Theme.space3
            Layout.topMargin: Theme.space2
            Layout.bottomMargin: Theme.space2
            spacing: Theme.space2

            BChip {
                id: projectChip
                objectName: "codeProjectChip"
                iconName: "folder"
                text: root.projectName !== "" ? root.projectName : qsTr("No project")
                interactive: root.path === ""
                Accessible.name: qsTr("Project: %1").arg(projectChip.text)
                onClicked: projectMenu.popup(projectChip, 0, projectChip.height)
            }
            // Where a thread works: its worktree's branch, else the project's folder.
            BChip {
                objectName: "codeWhere"
                visible: root.path !== ""
                iconName: root.worktree ? "git-branch" : "hard-drive"
                text: root.worktree ? root.branch : qsTr("Local")
                C.ToolTip.visible: hovered
                C.ToolTip.text: root.folder
            }
            // A new thread: in the project's folder, or in a worktree of its own.
            BSegmented {
                objectName: "codeEnvironment"
                visible: root.path === "" && root.project !== ""
                accessibleName: qsTr("Where the thread works")
                model: [{ value: "local", text: qsTr("Local") }, { value: "worktree", text: qsTr("New worktree") }]
                currentValue: root.useWorktree ? "worktree" : "local"
                onActivated: (value) => {
                    root.useWorktree = value === "worktree"
                    AppSettings.setValue("code/newWorktree", root.useWorktree)
                }
            }
            BSpinner {
                visible: root.makingWorktree
                size: Theme.iconSize
            }
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
            }
            RowLayout {
                id: tools
                spacing: Theme.space1
                Repeater {
                    model: root.scripts
                    delegate: BButton {
                        id: scriptButton
                        required property var modelData
                        required property int index
                        readonly property string scriptCommand: AppController.routeText(scriptButton.modelData, "command")
                        readonly property string scriptName: AppController.routeText(scriptButton.modelData, "name")
                        objectName: "codeScript_" + scriptButton.index
                        size: "small"
                        variant: "secondary"
                        iconName: "play"
                        text: scriptButton.scriptName
                        enabled: root.folder !== ""
                        onClicked: root.runScript(scriptButton.scriptCommand, scriptButton.scriptName)
                    }
                }
                BIconButton {
                    objectName: "codeOpenEditor"
                    iconName: "code"
                    label: qsTr("Open in editor")
                    enabled: root.folder !== ""
                    onClicked: {
                        if (!Workspace.projects.openInEditor(root.folder))
                            AppController.toast(qsTr("The editor could not be started: set its command in Settings → Extensions → Code mode."), "error")
                    }
                }
                BIconButton {
                    objectName: "codeTerminalToggle"
                    iconName: "terminal"
                    label: root.terminalOpen ? qsTr("Hide the terminal") : qsTr("Show the terminal")
                    checked: root.terminalOpen
                    onClicked: root.setTerminalOpen(!root.terminalOpen)
                }
                BIconButton {
                    objectName: "codeChangesToggle"
                    iconName: "git-compare"
                    label: root.changesOpen ? qsTr("Hide the changes") : qsTr("Show the changes")
                    checked: root.changesOpen
                    onClicked: root.setChangesOpen(!root.changesOpen)
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.borderSubtle
        }

        // ---- The thread, its terminal under it, its changes beside it --------------------------
        C.SplitView {
            id: split
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal
            handle: BSplitHandle {}

            C.SplitView {
                id: column
                C.SplitView.fillWidth: true
                C.SplitView.minimumWidth: 320
                orientation: Qt.Vertical
                handle: BSplitHandle {}

                ChatView {
                    id: chat
                    objectName: "codeChat"
                    C.SplitView.fillHeight: true
                    C.SplitView.minimumHeight: 200
                    route: root.path !== "" ? root.route : { view: "code", new: true, project: root.project }
                    routeView: "code"
                    showList: false
                    project: root.project
                    wantsWorktree: root.useWorktree
                    onWorktreeWanted: (text) => {
                        if (root.makingWorktree)
                            return
                        root.makingWorktree = Workspace.projects.createWorktree(root.project, text)
                        if (!root.makingWorktree)
                            AppController.toast(qsTr("No git here: the thread works in the project's folder."), "warning")
                    }
                }
                // The thread's terminals (TerminalRegistry's: kept while another thread is shown),
                // the panel made once first shown.
                Loader {
                    id: terminalLoader
                    visible: root.terminalOpen && root.folder !== ""
                    active: root.terminalWanted
                    C.SplitView.preferredHeight: 260
                    C.SplitView.minimumHeight: 120
                    onLoaded: {
                        root.runPending()
                        root.runTerminalAction()
                    }
                    sourceComponent: TerminalPanel {
                        objectName: "codeTerminal"
                        threadKey: root.terminalKey
                        folder: root.folder
                        shown: root.terminalOpen && root.folder !== ""
                        dark: Theme.dark
                        // (A link a program printed, Ctrl+click: opened as a note's link would be.)
                        onLinkActivated: (url) => pageTools.openLink(url, "")
                        // (A file path: in the editor for now.)
                        onFileActivated: (path, line, column) => {
                            if (!Workspace.projects.openInEditor(path))
                                AppController.toast(qsTr("The editor could not be started: set its command in Settings → Extensions → Code mode."), "error")
                        }
                        onAddToChat: (text) => chat.addToComposer(text)
                    }
                }
            }
            GitChangesPanel {
                id: changes
                objectName: "codeChanges"
                visible: root.changesOpen && root.folder !== ""
                C.SplitView.preferredWidth: root.wide ? 420 : 320
                C.SplitView.minimumWidth: 260
                folder: root.folder
                // The last turn's snapshot (WorkspaceChats.checkpoints): "This turn" and its revert.
                checkpoint: root.path !== "" ? Workspace.chats.lastCheckpoint(root.path, Workspace.chats.checkpointSerial) : ""
                checkpointLabel: qsTr("The agent's last turn")
                // (While the agent works: its changes as they come.)
                pollInterval: chat.busyHere ? 2000 : 0
                onActionFinished: (action, ok, message, url) => {
                    if (message !== "")
                        AppController.toast(url !== "" ? message + "\n" + url : message, ok ? "success" : "error")
                }
            }
        }
    }

    PageTools {
        id: pageTools
        workspace: Workspace
    }

    BMenu {
        id: projectMenu
        objectName: "codeProjectPick"
        Instantiator {
            model: Workspace.projects.list
            delegate: BMenuItem {
                id: pickItem
                required property var modelData
                readonly property string projectId: AppController.routeText(pickItem.modelData, "id")
                text: AppController.routeText(pickItem.modelData, "name")
                iconName: pickItem.projectId === root.project ? "check" : "folder"
                onTriggered: AppController.replaceRoute({ view: "code", new: true, project: pickItem.projectId })
            }
            onObjectAdded: (index, object) => projectMenu.insertItem(index, object as Item)
            onObjectRemoved: (index, object) => projectMenu.removeItem(object as Item)
        }
        BMenuSeparator {}
        BMenuItem {
            text: qsTr("Add a project…")
            iconName: "folder-plus"
            onTriggered: folderPicker.open()
        }
    }

    FilePicker {
        id: folderPicker
        objectName: "codeViewFolderDialog"
        title: qsTr("Choose the folder of a project")
        fileMode: FilePicker.OpenFolder
        dark: Theme.dark
        colors: Theme.colors
        onAccepted: {
            const id = Workspace.projects.add("" + folderPicker.selectedFolder)
            if (id === "")
                AppController.toast(qsTr("That folder could not be added."), "error")
            else
                AppController.replaceRoute({ view: "code", new: true, project: id })
        }
    }
}
