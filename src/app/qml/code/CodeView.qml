pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic as C
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// CodeView — route "code", Code mode's main view (docs/CODE.md, docs/UI.md "Code mode"), as T3 Code:
// an AI agent's thread of a project — the conversation (ChatView as a thread: routeView "code", no
// list; CodeSidebar lists the threads) with, beside it, the Changes panel (GitChangesPanel: the
// project's working tree against git, a turn's changes, commit / push / pull request) and under it
// the Terminal panel (TerminalPanel: shells in the thread's folder; the project's script buttons
// run there).
//
//   {view: "code"}                                   a new thread of the last project used (or the first)
//   {view: "code", new: true, project, env?}         a new thread of `project` (env "local" / "worktree")
//   {view: "code", new: true, project, worktree, branch, pr?}   … in that worktree (on that pull request)
//   {view: "code", path: "AI/….md"}                  a thread
//   {view: "code", addProject: true}                 … and the folder picker (the palette's "Add a project…")
//
// The header: the project (for a new thread a menu of them), the thread's name (editable: a double
// click, F2), where it works — a thread: its branch (the branch picker, Ctrl+Shift+G: switch, create,
// check out a pull request); a new thread: Local / New worktree (from a base branch) / an existing
// worktree (Ctrl+Shift+X), the project's default from CodeProjects.newThreadEnv — the project's
// script buttons, "Open in…" (CodeOpenIn, Ctrl+O) and the panels' toggles (kept per device:
// "code/changesOpen" Ctrl+D, "code/terminalOpen"). A new worktree is made before the first message
// is sent; the project's scripts that run then run in its terminal, and those to wait for hold the
// message (CodeSetupBanner) until they ended well — or the user sends it anyway.
// A project of another computer (its threads run there, docs/CODE.md "Threads on the phone"): the
// computer's name in the header, no terminal, editor or scripts here, and CodeRemoteChanges — that
// computer's changes of the thread, read-only — in place of the Changes panel (under the thread
// when narrow, opened with the changes' toggle).
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
    readonly property string projectFolder: Workspace.projects.folderOf(root.project)
    /// Where it works: a thread's folder; a new thread's — the worktree it will work in (an existing
    /// one chosen, or the one just made for it), else the project's.
    readonly property string folder: root.path !== "" ? AppController.routeText(root.place, "folder")
                                     : root.envMode === "existing" && root.envWorktree !== "" ? root.envWorktree
                                     : root.envMode === "worktree" && root.madeWorktree !== "" ? root.madeWorktree
                                     : root.projectFolder
    readonly property string branch: AppController.routeText(root.place, "branch")
    readonly property bool worktree: AppController.routeText(root.place, "worktree") !== ""
    /// The project is another computer's: its name, the thread's worktree there.
    readonly property bool remote: root.path !== "" ? AppController.routeFlag(root.place, "remote")
                                                    : Workspace.projects.isRemote(root.project)
    readonly property string host: root.path !== "" ? AppController.routeText(root.place, "host")
                                                    : AppController.routeText(Workspace.projects.project(root.project), "host")
    /// Narrow (a phone): the other computer's changes under the thread, only when asked for.
    property bool remoteChangesShown: false
    /// The branch checked out in the folder now (the Changes panel's git).
    readonly property string currentBranch: changes.git.branch
    readonly property list<var> scripts: AppController.routeList(Workspace.projects.project(root.project), "scripts")
    property bool changesOpen: AppSettings.value("code/changesOpen", true) !== false
    property bool terminalOpen: AppSettings.value("code/terminalOpen", false) === true
    /// A worktree is being made for the new thread's first message.
    property bool makingWorktree: false
    readonly property string title: root.path !== "" ? chat.title : root.projectName !== "" ? qsTr("New thread") : qsTr("Code")
    readonly property bool wide: root.width >= 900

    // ---- where a new thread works (T3 Code's environment) -------------------------------------------
    /// "local" (the project's folder), "worktree" (a new one, made before the first message) or
    /// "existing" (`envWorktree`, on `envBranch`).
    property string envMode: "local"
    property string envWorktree
    property string envBranch
    /// The branch a new worktree starts from ("" the project's HEAD).
    property string baseBranch
    /// The pull request a new thread works on ("#123 Title", "" none): its agent is told.
    property string pullRequest
    /// The project's worktrees (listWorktrees, for the environment menu).
    property list<var> worktrees
    /// The worktree made for the new thread (its setup may still run: the message waits).
    property string madeWorktree
    property string madeBranch

    function firstProject(): string {
        const all = Workspace.projects.list
        return all.length > 0 ? AppController.routeText(all[0], "id") : ""
    }
    function refreshPlace(): void {
        root.place = root.path !== "" ? Workspace.chats.threadPlace(root.path) : root.noPlace
    }
    readonly property var noPlace: ({})
    function refreshRemoteChanges(): void {
        const wide = remoteWide.item as CodeRemoteChanges
        if (wide !== null)
            wide.refresh()
        const narrow = remoteNarrow.item as CodeRemoteChanges
        if (narrow !== null)
            narrow.refresh()
    }
    readonly property list<string> noStrings: []
    function setChangesOpen(on: bool): void {
        root.changesOpen = on
        AppSettings.setValue("code/changesOpen", on)
    }
    function setTerminalOpen(on: bool): void {
        root.terminalOpen = on
        AppSettings.setValue("code/terminalOpen", on)
    }
    /// The route's choice of where a new thread works, else the project's default (newThreadEnv).
    function resetEnvironment(): void {
        const env = AppController.routeText(root.route, "env")
        root.envWorktree = AppController.routeText(root.route, "worktree")
        root.envBranch = AppController.routeText(root.route, "branch")
        root.envMode = root.envWorktree !== "" ? "existing"
                     : env === "local" || env === "worktree" ? env
                     : Workspace.projects.newThreadEnv(root.project)
        root.baseBranch = ""
        root.madeWorktree = ""
        root.madeBranch = ""
        root.pullRequest = AppController.routeText(root.route, "pr")
        root.settingUp = ""
        root.setupSession = null
        root.setupCommands = root.noStrings
        root.setupNames = root.noStrings
    }
    function setEnvironment(mode: string, worktree: string, branch: string): void {
        if (mode !== "worktree") {
            root.madeWorktree = ""
            root.madeBranch = ""
        }
        root.envMode = mode
        root.envWorktree = worktree
        root.envBranch = branch
        root.applyEnvironment()
    }
    /// ChatView's new thread works where the environment says (an existing worktree: there; a new
    /// one: once made).
    function applyEnvironment(): void {
        if (root.path !== "")
            return
        chat.newWorktree = root.envMode === "existing" ? root.envWorktree : ""
        chat.newBranch = root.envMode === "existing" ? root.envBranch : ""
        chat.newPullRequest = root.pullRequest
    }
    /// The project's defaults for a new thread: its agent (when it is one set up here).
    function applyDefaults(): void {
        if (root.path === "")
            root.applyEnvironment()
        const agent = AppController.routeText(Workspace.projects.project(root.project), "agent")
        if (root.path === "" && agent !== "" && Workspace.chats.partnerLabel(agent) !== "")
            chat.newPartner = agent
    }
    /// The first message goes: in the worktree made for it, told of its pull request.
    function sendNow(): void {
        if (root.envMode === "worktree") {
            chat.newWorktree = root.madeWorktree
            chat.newBranch = root.madeBranch
        }
        chat.newPullRequest = root.pullRequest
        chat.send()
    }
    function envLabel(): string {
        if (root.envMode === "worktree")
            return qsTr("New worktree")
        if (root.envMode === "existing")
            return qsTr("Worktree: %1").arg(root.envBranch !== "" ? root.envBranch : root.envWorktree)
        return qsTr("Local")
    }

    // ---- scripts in the terminal ----------------------------------------------------------------------
    /// Runs `command` in a new tab of the thread's terminal (opened when hidden).
    function runScript(command: string, name: string): void {
        root.setTerminalOpen(true)
        root.pendingCommands = Js.appendedString(root.pendingCommands, command)
        root.pendingNames = Js.appendedString(root.pendingNames, name)
        root.runPending()
    }
    function runScriptById(id: string): void {
        const command = root.scriptField(id, "command")
        const name = root.scriptField(id, "name")
        if (command !== "")
            root.runScript(command, name)
    }
    function scriptField(id: string, field: string): string {
        return Workspace.projects.scriptField(root.project, id, field)
    }
    /// One field of the project's setup scripts waited for or not (CodeProjects.setupList).
    function setupOf(field: string, waited: bool): list<string> {
        return Workspace.projects.setupList(root.project, field, waited)
    }
    /// The scripts asked for, run once the terminal of the folder is there.
    property list<string> pendingCommands
    property list<string> pendingNames
    function runPending(): void {
        const panel = terminalLoader.item as TerminalPanel
        if (panel === null || panel.folder !== root.folder)
            return
        if (!panel.available) {
            root.pendingCommands = root.noStrings
            root.pendingNames = root.noStrings
            if (root.settingUp === "running") {
                AppController.toast(qsTr("No terminal on this device: the worktree's setup scripts did not run."), "warning")
                root.finishSetup()
            }
            return
        }
        while (root.pendingCommands.length > 0) {
            const command = root.pendingCommands[0]
            const name = root.pendingNames.length > 0 ? root.pendingNames[0] : ""
            root.pendingCommands = Js.removedStringAt(root.pendingCommands, 0)
            root.pendingNames = Js.removedStringAt(root.pendingNames, 0)
            panel.run(command, name)
        }
        root.runNextSetup()
    }

    // ---- a new worktree's setup (T3 Code's runOnWorktreeCreate) -----------------------------------------
    /// "" none, "running" (the message waits for the scripts), "failed" (the user decides).
    property string settingUp
    property string setupDetail
    /// The scripts to wait for not started yet, and the one that runs now.
    property list<string> setupCommands
    property list<string> setupNames
    property TerminalSession setupSession: null
    property string setupName
    /// The worktree is made: its setup scripts start (those not waited for at once, the others one
    /// after another), and the message goes now or once the waited ones ended.
    function startSetup(): void {
        if (Workspace.projects.setupCount(root.project) === 0) {
            root.sendNow()
            return
        }
        const commands = root.setupOf("command", false)
        root.pendingCommands = Js.concatStrings(root.pendingCommands, commands)
        const names = root.setupOf("name", false)
        root.pendingNames = Js.concatStrings(root.pendingNames, names)
        root.setupCommands = root.setupOf("command", true)
        root.setupNames = root.setupOf("name", true)
        root.setTerminalOpen(true)
        if (root.setupCommands.length === 0) {
            root.runPending()
            root.sendNow()
            return
        }
        root.setupDetail = root.setupNames.join(", ")
        root.settingUp = "running"
        root.runPending()
    }
    function runNextSetup(): void {
        const panel = terminalLoader.item as TerminalPanel
        if (root.settingUp !== "running" || root.setupSession !== null || panel === null || panel.folder !== root.folder)
            return
        if (root.setupCommands.length === 0) {
            root.finishSetup()
            return
        }
        const command = root.setupCommands[0]
        root.setupName = root.setupNames.length > 0 ? root.setupNames[0] : command
        root.setupCommands = Js.removedStringAt(root.setupCommands, 0)
        root.setupNames = Js.removedStringAt(root.setupNames, 0)
        panel.run(command, root.setupName)
        root.setupSession = panel.currentSession
        if (root.setupSession === null)
            root.setupFailed(qsTr("%1: it did not start").arg(root.setupName))
    }
    function setupFailed(why: string): void {
        root.setupSession = null
        root.setupDetail = why
        root.settingUp = "failed"
    }
    /// The setup is over (or skipped): the message goes.
    function finishSetup(): void {
        root.settingUp = ""
        root.setupSession = null
        root.setupCommands = root.noStrings
        root.setupNames = root.noStrings
        root.sendNow()
    }
    Connections {
        target: root.setupSession
        function onExited(exitCode: int) {
            if (exitCode !== 0) {
                root.setupFailed(qsTr("%1: exit code %2").arg(root.setupName).arg(exitCode))
                return
            }
            root.setupSession = null
            root.runNextSetup()
        }
        function onErrorStringChanged() {
            const session = root.setupSession
            if (session !== null && session.errorString !== "")
                root.setupFailed(root.setupName + ": " + session.errorString)
        }
    }

    // ---- branches and pull requests -----------------------------------------------------------------
    /// Why the folder's branch cannot be switched now ("" it can): never under an agent at work.
    function switchBlocked(): string {
        if (!Workspace.projects.gitAvailable)
            return qsTr("git is not on this computer.")
        if (root.path !== "" && chat.busyHere)
            return qsTr("The agent works in this thread: switch the branch once its turn ended.")
        if (Workspace.chats.runningIn(root.folder))
            return qsTr("Another thread works in this folder: switch the branch once its turn ended.")
        return ""
    }
    function openBranches(): void {
        if (root.folder === "")
            return
        if (root.path === "" && root.envMode === "worktree")
            branchPicker.openFor(root.projectFolder, "base", "")
        else
            branchPicker.openFor(root.folder, "switch", root.switchBlocked())
    }
    function branchPicked(name: string, remote: bool, worktree: string): void {
        if (branchPicker.mode === "base") {
            root.baseBranch = name
            return
        }
        // A branch checked out in another worktree: the thread works there.
        if (worktree !== "" && worktree !== root.folder) {
            const own = worktree === root.projectFolder
            if (root.path !== "") {
                Workspace.chats.setThreadWorktree(root.path, own ? "" : worktree, own ? "" : name)
                root.refreshPlace()
            } else {
                root.setEnvironment(own ? "local" : "existing", own ? "" : worktree, own ? "" : name)
            }
            return
        }
        if (worktree === root.folder && !remote)
            return // (the branch checked out here)
        const blocked = root.switchBlocked()
        if (blocked !== "") {
            AppController.toast(blocked, "warning")
            return
        }
        root.switching = Workspace.projects.switchBranch(root.folder, name)
    }
    property bool switching: false
    Connections {
        target: Workspace.projects
        function onBranchSwitched(folder: string, branch: string, ok: bool, message: string) {
            if (!root.switching || folder !== root.folder)
                return
            root.switching = false
            AppController.toast(ok ? qsTr("Now on %1").arg(branch) : message, ok ? "success" : "error")
            // (A thread with a worktree of its own: its page names the branch.)
            if (ok && root.path !== "" && root.worktree)
                Workspace.chats.setThreadWorktree(root.path, root.folder, branch)
            changes.git.refresh()
            root.refreshPlace()
        }
        function onWorktreesListed(project: string, worktrees: list<var>, error: string) {
            if (project === root.project)
                root.worktrees = worktrees
        }
    }
    function pullRequestReady(number: int, folder: string, branch: string, prTitle: string): void {
        const what = prTitle !== "" ? "#" + number + " " + prTitle : "#" + number
        if (folder !== "") {
            // In a worktree of its own: a new thread there, told which pull request it is.
            if (root.path === "") {
                root.pullRequest = what
                root.setEnvironment("existing", folder, branch)
            } else {
                AppController.navigate({ view: "code", new: true, project: root.project, worktree: folder, branch: branch, pr: what })
            }
            return
        }
        AppController.toast(qsTr("Pull request %1 checked out in the project's folder (%2)").arg(what).arg(branch), "success")
        if (root.path === "") {
            root.pullRequest = what
            root.setEnvironment("local", "", "")
        }
        changes.git.refresh()
    }

    onPathChanged: root.refreshPlace()
    onProjectChanged: {
        if (root.path === "" && root.project !== "")
            AppSettings.setValue("code/lastProject", root.project)
    }
    onRouteChanged: {
        root.refreshPlace()
        // A new thread's route: where it works from the route or the project's default. A thread's
        // (its first message just sent): the new thread's worktree stays this view's folder until
        // `path` follows the route — `root.path` may still be the one before here — so its
        // terminal, with the setup scripts in it, stays.
        if (AppController.routeText(root.route, "path") === "") {
            chat.newWorktree = ""
            chat.newBranch = ""
            chat.newPullRequest = ""
            root.resetEnvironment()
            defaultsLater.restart()
        } else {
            root.settingUp = ""
            root.setupSession = null
        }
        if (AppController.routeFlag(root.route, "addProject"))
            folderPicker.open()
    }
    // (After ChatView took the route too: its new thread's partner and place.)
    Later {
        id: defaultsLater
        onTriggered: root.applyDefaults()
    }
    Component.onCompleted: {
        root.refreshPlace()
        root.resetEnvironment()
        defaultsLater.restart()
        root.followFolder()
        if (AppController.routeFlag(root.route, "addProject"))
            folderPicker.open()
    }
    // The palette's and the shortcuts' Code mode commands (CommandModel: "code:<what>").
    Connections {
        target: AppController
        function onUiActionRequested(action: string) {
            if (!root.visible || Workspace.projects.count === 0)
                return
            if (Js.startsWith(action, "code:script:")) {
                root.runScriptById(Js.sliceFrom(action, 12))
                return
            }
            switch (action) {
            case "code:renameThread": titleItem.edit(); break
            case "code:branches": root.openBranches(); break
            case "code:environment":
                if (root.path === "")
                    envMenu.openBelow(envChip)
                break
            case "code:openInEditor": openIn.open(openIn.current); break
            case "code:toggleChanges": root.setChangesOpen(!root.changesOpen); break
            }
        }
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
            root.refreshRemoteChanges()
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
            root.madeWorktree = folder
            root.madeBranch = branch
            root.startSetup()
        }
    }

    /// The folder the terminal works in: the thread's, taken when the terminal is first shown or
    /// the thread's folder changes (its shells end then).
    property string terminalFolder
    property bool terminalWanted: false
    onFolderChanged: root.followFolder()
    onTerminalOpenChanged: root.followFolder()
    function followFolder(): void {
        if (root.terminalOpen)
            root.terminalWanted = true
        // (No folder for a moment — a new thread's route replaced by its page's before its place is
        // read —: the terminal stays, with the scripts running in it.)
        if (root.folder === "")
            return
        if (root.terminalWanted && root.terminalFolder !== root.folder) {
            root.terminalFolder = ""
            root.terminalFolder = root.folder
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

        // ---- The thread's header: project, name, where it works, scripts, editor, panels --------------
        RowLayout {
            id: header
            objectName: "codeHeader"
            Layout.fillWidth: true
            // (Never wider than the view: on a phone it would push the thread out of the window.)
            Layout.maximumWidth: root.width - 2 * Theme.space3
            Layout.leftMargin: Theme.space3
            Layout.rightMargin: Theme.space3
            Layout.topMargin: Theme.space2
            Layout.bottomMargin: Theme.space2
            spacing: Theme.space2

            BChip {
                id: projectChip
                objectName: "codeProjectChip"
                // (Another computer's project: that computer's name with it.)
                iconName: root.remote ? "laptop" : "folder"
                text: root.projectName === "" ? qsTr("No project")
                      : root.remote && root.host !== "" && root.path !== "" ? root.projectName + " · " + root.host
                      : root.projectName
                interactive: root.path === ""
                Accessible.name: qsTr("Project: %1").arg(projectChip.text)
                onClicked: projectMenu.popup(projectChip, 0, projectChip.height)
            }
            CodeThreadTitle {
                id: titleItem
                Layout.fillWidth: true
                Layout.minimumWidth: 60
                path: root.path
                text: root.title
            }
            // A thread: where it works — its folder's branch (the branch picker).
            BChip {
                id: whereChip
                objectName: "codeWhere"
                visible: root.path !== ""
                iconName: root.worktree ? "git-branch" : "hard-drive"
                text: root.currentBranch !== "" ? root.currentBranch : root.worktree ? root.branch : qsTr("Local")
                interactive: root.folder !== "" && !root.remote
                Accessible.name: qsTr("Branch: %1").arg(whereChip.text)
                C.ToolTip.visible: hovered
                C.ToolTip.text: root.worktree ? qsTr("A worktree of its own: %1").arg(root.folder)
                                              : qsTr("The project's folder: %1").arg(root.folder)
                onClicked: root.openBranches()
            }
            // A new thread: in the project's folder, in a new worktree, or in an existing one.
            BChip {
                id: envChip
                objectName: "codeEnvironment"
                visible: root.path === "" && root.project !== ""
                iconName: root.envMode === "local" ? "hard-drive" : "folder-tree"
                text: root.envLabel()
                interactive: !root.makingWorktree && root.settingUp === ""
                Accessible.name: qsTr("Where the thread works: %1").arg(envChip.text)
                onClicked: envMenu.openBelow(envChip)
            }
            // … a new worktree's base branch / a Local one's branch.
            BChip {
                id: baseChip
                objectName: "codeBaseBranch"
                visible: root.path === "" && root.project !== "" && root.envMode !== "existing" && root.madeWorktree === ""
                         && !root.remote
                iconName: "git-branch"
                text: root.envMode === "worktree"
                      ? qsTr("From %1").arg(root.baseBranch !== "" ? root.baseBranch : root.currentBranch !== "" ? root.currentBranch : "HEAD")
                      : root.currentBranch !== "" ? root.currentBranch : qsTr("Branch")
                interactive: !root.makingWorktree && root.settingUp === ""
                Accessible.name: root.envMode === "worktree" ? qsTr("The new worktree's base branch: %1").arg(baseChip.text)
                                                             : qsTr("Branch: %1").arg(baseChip.text)
                onClicked: root.openBranches()
            }
            BChip {
                objectName: "codePullRequestChip"
                visible: root.path === "" && root.pullRequest !== ""
                size: "small"
                iconName: "git-pull-request"
                text: root.pullRequest
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
                // The project's first scripts as buttons, all of them in the menu beside.
                Repeater {
                    model: root.remote ? 0 : root.scripts.length > 3 ? 2 : root.scripts.length
                    delegate: BButton {
                        id: scriptButton
                        required property int index
                        readonly property var script: root.scripts[scriptButton.index]
                        readonly property string scriptCommand: AppController.routeText(scriptButton.script, "command")
                        readonly property string scriptName: AppController.routeText(scriptButton.script, "name")
                        objectName: "codeScript_" + scriptButton.index
                        size: "small"
                        variant: "secondary"
                        iconName: AppController.routeText(scriptButton.script, "iconName")
                        text: scriptButton.scriptName
                        enabled: root.folder !== ""
                        Accessible.description: scriptButton.scriptCommand
                        onClicked: root.runScript(scriptButton.scriptCommand, scriptButton.scriptName)
                    }
                }
                BIconButton {
                    id: scriptsMore
                    objectName: "codeScriptsMenuButton"
                    visible: root.scripts.length > 3 && !root.remote
                    size: "small"
                    iconName: "play"
                    label: qsTr("Scripts")
                    enabled: root.folder !== ""
                    onClicked: scriptsMenu.popup(scriptsMore, 0, scriptsMore.height)
                }
                CodeOpenIn {
                    id: openIn
                    visible: !root.remote
                    folder: root.folder
                }
                BIconButton {
                    objectName: "codeTerminalToggle"
                    visible: !root.remote
                    iconName: "terminal"
                    label: root.terminalOpen ? qsTr("Hide the terminal") : qsTr("Show the terminal")
                    checked: root.terminalOpen
                    onClicked: root.setTerminalOpen(!root.terminalOpen)
                }
                BIconButton {
                    objectName: "codeChangesToggle"
                    iconName: "git-compare"
                    readonly property bool shown: root.remote && !root.wide ? root.remoteChangesShown : root.changesOpen
                    label: shown ? qsTr("Hide the changes") : qsTr("Show the changes")
                    shortcut: ShortcutMap.sequence("code.toggleChanges")
                    checked: shown
                    onClicked: {
                        if (root.remote && !root.wide)
                            root.remoteChangesShown = !root.remoteChangesShown
                        else
                            root.setChangesOpen(!root.changesOpen)
                    }
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.borderSubtle
        }
        // A new worktree being set up before the first message goes (or its setup failed).
        CodeSetupBanner {
            Layout.fillWidth: true
            visible: root.settingUp !== ""
            phase: root.settingUp === "failed" ? "failed" : "running"
            detail: root.settingUp === "failed" ? root.setupDetail : root.setupName !== "" ? root.setupName : root.setupDetail
            onSendAnyway: root.finishSetup()
            onDontSend: {
                // (The message stays in the field; the worktree stays: the next Send goes there.)
                root.settingUp = ""
                root.setupSession = null
                root.setupCommands = root.noStrings
                root.setupNames = root.noStrings
            }
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
                    wantsWorktree: root.envMode === "worktree"
                    onWorktreeWanted: (text) => {
                        // (While it is made or set up the message waits; made already — its setup
                        // failed and was not sent —: it goes there now.)
                        if (root.makingWorktree || root.settingUp !== "")
                            return
                        if (root.madeWorktree !== "") {
                            root.sendNow()
                            return
                        }
                        root.makingWorktree = Workspace.projects.createWorktree(root.project, text, root.baseBranch)
                        if (!root.makingWorktree)
                            AppController.toast(qsTr("No git here: the thread works in the project's folder."), "warning")
                    }
                }
                // The thread's terminal: made for its folder (another thread's folder: a new one),
                // only once shown.
                Loader {
                    id: terminalLoader
                    visible: root.terminalOpen && root.folder !== ""
                    active: root.terminalWanted && root.terminalFolder !== ""
                    C.SplitView.preferredHeight: 260
                    C.SplitView.minimumHeight: 120
                    onLoaded: root.runPending()
                    sourceComponent: TerminalPanel {
                        objectName: "codeTerminal"
                        folder: root.terminalFolder
                        dark: Theme.dark
                        // (A link a program printed, Ctrl+click: opened as a note's link would be.)
                        onLinkActivated: (url) => pageTools.openLink(url, "")
                    }
                }
                // Another computer's project, narrow: its changes under the thread.
                Loader {
                    id: remoteNarrow
                    visible: active
                    active: root.remote && !root.wide && root.remoteChangesShown && root.project !== ""
                    C.SplitView.preferredHeight: 320
                    C.SplitView.minimumHeight: 160
                    sourceComponent: CodeRemoteChanges {
                        project: root.project
                        worktree: AppController.routeText(root.place, "worktree")
                        checkpoint: root.path !== "" ? Workspace.chats.lastCheckpoint(root.path, Workspace.chats.checkpointSerial) : ""
                        host: root.host
                    }
                }
            }
            // … wide: beside it, as the Changes panel.
            Loader {
                id: remoteWide
                visible: active
                active: root.remote && root.wide && root.changesOpen && root.project !== ""
                C.SplitView.preferredWidth: 420
                C.SplitView.minimumWidth: 260
                sourceComponent: CodeRemoteChanges {
                    project: root.project
                    worktree: AppController.routeText(root.place, "worktree")
                    checkpoint: root.path !== "" ? Workspace.chats.lastCheckpoint(root.path, Workspace.chats.checkpointSerial) : ""
                    host: root.host
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

    // Where a new thread works: Local, a new worktree, or one of the project's worktrees.
    BMenu {
        id: envMenu
        objectName: "codeEnvironmentMenu"
        function openBelow(item: Item): void {
            root.worktrees = root.noVars
            Workspace.projects.listWorktrees(root.project)
            envMenu.popup(item, 0, item.height)
        }
        BMenuItem {
            objectName: "codeEnvLocal"
            text: qsTr("Local")
            hint: qsTr("the project's folder")
            iconName: "hard-drive"
            checkable: true
            checked: root.envMode === "local"
            onTriggered: root.setEnvironment("local", "", "")
        }
        BMenuItem {
            objectName: "codeEnvWorktree"
            text: qsTr("New worktree")
            hint: qsTr("a branch and folder of its own")
            iconName: "folder-tree"
            checkable: true
            checked: root.envMode === "worktree"
            enabled: Workspace.projects.gitAvailable
            onTriggered: root.setEnvironment("worktree", "", "")
        }
        BMenuSeparator { visible: root.worktrees.length > 0 }
        Instantiator {
            model: root.worktrees
            delegate: BMenuItem {
                id: treeItem
                required property var modelData
                required property int index
                readonly property string treeFolder: AppController.routeText(treeItem.modelData, "folder")
                readonly property string treeBranch: AppController.routeText(treeItem.modelData, "branch")
                objectName: "codeEnvExisting_" + treeItem.index
                text: treeItem.treeBranch !== "" ? qsTr("Worktree: %1").arg(treeItem.treeBranch) : treeItem.treeFolder
                hint: Js.sliceFrom(treeItem.treeFolder, Js.lastIndexOf(treeItem.treeFolder, "/") + 1)
                iconName: "git-branch"
                checkable: true
                checked: root.envMode === "existing" && root.envWorktree === treeItem.treeFolder
                onTriggered: root.setEnvironment("existing", treeItem.treeFolder, treeItem.treeBranch)
            }
            onObjectAdded: (index, object) => envMenu.insertItem(index + 3, object as Item)
            onObjectRemoved: (index, object) => envMenu.removeItem(object as Item)
        }
    }
    readonly property list<var> noVars: []

    BMenu {
        id: scriptsMenu
        objectName: "codeScriptsMenu"
        Instantiator {
            model: root.scripts
            delegate: BMenuItem {
                id: scriptItem
                required property var modelData
                text: AppController.routeText(scriptItem.modelData, "name")
                hint: AppController.routeText(scriptItem.modelData, "shortcut")
                iconName: AppController.routeText(scriptItem.modelData, "iconName")
                onTriggered: root.runScript(AppController.routeText(scriptItem.modelData, "command"), scriptItem.text)
            }
            onObjectAdded: (index, object) => scriptsMenu.insertItem(index, object as Item)
            onObjectRemoved: (index, object) => scriptsMenu.removeItem(object as Item)
        }
    }

    CodeBranchPicker {
        id: branchPicker
        projectFolder: root.projectFolder
        onBranchPicked: (name, remote, worktree) => root.branchPicked(name, remote, worktree)
        onCreateRequested: (name) => {
            const blocked = root.switchBlocked()
            if (blocked !== "") {
                AppController.toast(blocked, "warning")
                return
            }
            root.switching = Workspace.projects.createBranch(branchPicker.folder, name)
        }
        onPullRequestRequested: (number) => prDialog.openFor(root.project, number, root.path !== "" || root.envMode !== "local")
    }

    CodePullRequestDialog {
        id: prDialog
        parent: T.Overlay.overlay
        onCheckedOut: (number, folder, branch, prTitle) => root.pullRequestReady(number, folder, branch, prTitle)
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
