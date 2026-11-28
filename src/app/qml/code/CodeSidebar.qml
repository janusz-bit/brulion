pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// CodeSidebar — Code mode's panel beside the rail (docs/CODE.md, docs/UI.md "Code mode"), as T3
// Code's: the projects of this device (Workspace.projects; pinned ones first, the others in the
// user's order — a drag with the mouse, or the menu's Move up / Move down), each with its threads
// (CodeProjectSection: ChatListModel of the project — a thread is an AI chat conversation whose page
// names it) and a draft of a new thread. A filter over the threads' titles and branches, their order
// (last activity / created; this device's), Ctrl+1–9 the n-th thread listed, the previous / next
// one (Ctrl+Shift+[ / ], uiActions "code:previousThread" / "code:nextThread"). A project's row folds
// its threads (kept per device: "code/collapsed"), its "+" starts a new thread there, its menu pins
// it, opens its folder in the editor or the file manager, edits it (CodeProjectDialog: its name,
// icon, defaults, scripts) and takes it off the list (its folder and threads stay). A thread's menu
// (CodeThreadRow) renames it, regenerates its title (the AI's; the thread shown follows it, a name
// kept or a failure is a toast), pins it, marks it unread, copies its path, branch or link, starts a
// new thread in its worktree, archives or deletes it — a thread with a worktree of its own that no
// other thread uses asks first whether the worktree goes too (CodeProjects.deletedWorktrees); Undo
// brings both back. "Add a project…" picks a folder or clones a repository (CodeCloneDialog: your
// repositories on GitHub, "owner/name" or any git address); a clone running or failed is a row above
// the projects (CodeCloneRow: its progress, Cancel; Retry, Dismiss). AppShell shows it in place of ShellSidebar
// while AppSettings.appMode is "code"; on phones it is the drawer's content.
// The projects of the user's other computers (CodeProjects' remote ones: their threads run there)
// follow this device's, with the computer's name and only their threads' entries in the menu; their
// lists are asked for again whenever the panel is shown.
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
    /// The filter's words (the threads' titles and branches).
    property string filter
    /// The project being dragged (its index in the list, -1 none) and where it would go.
    property int dragFrom: -1
    property int dropAt: -1

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
    function section(i: int): CodeProjectSection {
        return projectsRepeater.itemAt(i) as CodeProjectSection
    }

    // ---- the threads in the sidebar's order: Ctrl+1–9, the previous and next one ------------------
    /// The path of the n-th thread listed (0-based; folded projects' threads are not listed), "" none.
    function threadAt(n: int): string {
        let left = n
        for (let i = 0; i < projectsRepeater.count; ++i) {
            const s = sidebar.section(i)
            if (s === null || !s.open)
                continue
            if (left < s.threadCount)
                return s.threadPathAt(left)
            left -= s.threadCount
        }
        return ""
    }
    /// Where the thread `path` is listed (-1 not: folded, filtered out) and how many are.
    function threadIndex(path: string): int {
        let before = 0
        for (let i = 0; i < projectsRepeater.count; ++i) {
            const s = sidebar.section(i)
            if (s === null || !s.open)
                continue
            const row = s.threadRow(path)
            if (row >= 0)
                return before + row
            before += s.threadCount
        }
        return -1
    }
    function threadTotal(): int {
        let n = 0
        for (let i = 0; i < projectsRepeater.count; ++i) {
            const s = sidebar.section(i)
            if (s !== null && s.open)
                n += s.threadCount
        }
        return n
    }
    function openThreadAt(n: int): void {
        const path = sidebar.threadAt(n)
        if (path !== "")
            sidebar.go({ view: "code", path: path })
    }
    /// The thread `delta` places from the one shown (the first or the last one from a new thread).
    function step(delta: int): void {
        const total = sidebar.threadTotal()
        if (total === 0)
            return
        const at = sidebar.threadIndex(sidebar.routePath)
        const next = at < 0 ? (delta > 0 ? 0 : total - 1) : (at + delta + total) % total
        sidebar.openThreadAt(next)
    }
    Connections {
        target: AppController
        function onUiActionRequested(action: string) {
            if (action === "code:previousThread")
                sidebar.step(-1)
            else if (action === "code:nextThread")
                sidebar.step(1)
            // Ctrl+1–9 in Code mode: the n-th thread listed (AppShell's keys; Notes mode: the tabs).
            else if (Js.startsWith(action, "code:thread:"))
                sidebar.openThreadAt(Math.floor(Js.parseInt(Js.sliceFrom(action, 12))))
        }
    }

    // ---- a project dragged to another place ----------------------------------------------------------
    function dragStarted(i: int): void {
        sidebar.dragFrom = i
        sidebar.dropAt = i
    }
    function dragMoved(i: int, dy: real): void {
        const s = sidebar.section(i)
        if (s === null)
            return
        const y = s.y + s.rowHeight / 2 + dy
        let at = projectsRepeater.count
        for (let k = 0; k < projectsRepeater.count; ++k) {
            const other = sidebar.section(k)
            if (other !== null && y < other.y + other.height / 2) {
                at = k
                break
            }
        }
        sidebar.dropAt = at
    }
    function dragEnded(i: int): void {
        const s = sidebar.section(i)
        const at = sidebar.dropAt
        sidebar.dragFrom = -1
        sidebar.dropAt = -1
        if (s === null || at < 0 || at === i || at === i + 1)
            return
        Workspace.projects.move(s.projectId, at > i ? at - 1 : at)
    }

    // ---- deleting a thread: its worktree too? ---------------------------------------------------------
    function deleteThread(path: string, title: string, worktree: string, branch: string, project: string): void {
        // (A scratch thread's folder stays on disk, as in T3 Code; no git worktree to take away.)
        if (worktree === "" || Workspace.projects.isScratch(project)) {
            sidebar.trashThread(path, project, "", "", false, false)
            return
        }
        // (Another thread works there too, or the device keeps worktrees: the thread goes alone.)
        if (sidebar.othersIn(worktree, path).length > 0 || Workspace.projects.deletedWorktrees === "keep" || !Workspace.projects.gitAvailable) {
            sidebar.trashThread(path, project, worktree, branch, false, false)
            return
        }
        deleteDialog.prepare(path, title, worktree, branch, project)
    }
    /// The threads but `path` that work in `worktree`.
    function othersIn(worktree: string, path: string): list<string> {
        return Js.withoutString(Workspace.chats.threadsIn(worktree), path)
    }
    /// What the last deletion moved to the trash (WorkspaceChats.lastRemoved).
    property list<string> removedPaths
    function trashThread(path: string, project: string, worktree: string, branch: string, removeWorktree: bool, force: bool): void {
        const current = path === sidebar.routePath
        if (Workspace.chats.remove(path) === "")
            return
        sidebar.removedPaths = Workspace.chats.lastRemoved()
        if (removeWorktree) {
            Workspace.projects.removeWorktree(project, worktree, force)
            const id = AppController.toastWithAction(qsTr("Thread moved to trash, its worktree deleted"), "info", qsTr("Undo"))
            Workspace.projects.keepUndo(id, sidebar.removedPaths, project, worktree, branch, true)
        } else {
            AppController.toastRestore(qsTr("Thread moved to trash"), qsTr("Undo"), sidebar.removedPaths)
        }
        if (current)
            AppController.replaceRoute({ view: "code" })
    }
    // "Regenerate title" (a thread's menu): the thread shown follows its new name; a name kept or a
    // failure is said.
    Connections {
        target: Workspace.chats
        function onTitleRegenerated(path: string, newPath: string, outcome: string, message: string) {
            if (outcome === "renamed" && path === sidebar.routePath)
                AppController.replaceRoute({ view: "code", path: newPath })
            else if (outcome === "unchanged")
                AppController.toast(qsTr("Title kept: it already fits the thread."), "info")
            else if (outcome === "failed")
                AppController.toast(qsTr("The title could not be regenerated: %1").arg(message), "error")
        }
    }
    Connections {
        target: Workspace.projects
        function onWorktreeRemoved(folder: string, ok: bool, message: string) {
            if (!ok)
                AppController.toast(qsTr("The worktree could not be deleted: %1").arg(message), "error")
        }
        function onWorktreeRestored(folder: string, ok: bool, message: string) {
            if (!ok)
                AppController.toast(qsTr("The worktree could not be made again: %1").arg(message), "error")
        }
    }

    color: Theme.bgSunken
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Projects")

    // (The other computers' projects: asked for again when shown.)
    function refreshRemote(): void {
        const remote = Workspace.projects.remote
        if (remote !== null && sidebar.visible)
            remote.refresh()
    }
    onVisibleChanged: sidebar.refreshRemote()
    Component.onCompleted: sidebar.refreshRemote()
    PageTools {
        id: pageTools
        workspace: Workspace
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: sidebar.topInset
        anchors.bottomMargin: sidebar.bottomInset
        spacing: 0

        // ---- Header: the mode's name, the threads' order, add a project ----------------------------
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
                id: sortButton
                objectName: "codeThreadSort"
                visible: Workspace.projects.count > 0
                size: "small"
                iconName: "arrow-down-wide-narrow"
                label: Workspace.projects.threadSort === "created" ? qsTr("Threads: newest first") : qsTr("Threads: last activity first")
                onClicked: sortMenu.popup(sortButton, 0, sortButton.height)
            }
            BIconButton {
                id: addButton
                objectName: "codeAddProject"
                size: "small"
                iconName: "folder-plus"
                label: qsTr("Add a project…")
                onClicked: addMenu.popup(addButton, 0, addButton.height)
            }
        }
        BSearchField {
            id: filterField
            objectName: "codeThreadFilter"
            visible: Workspace.projects.count > 0
            Layout.fillWidth: true
            Layout.leftMargin: Theme.space3
            Layout.rightMargin: Theme.space3
            Layout.bottomMargin: Theme.space2
            size: "small"
            placeholderText: qsTr("Filter threads")
            accessibleName: qsTr("Filter threads by title or branch")
            onSearch: (query) => sidebar.filter = query
        }

        Flickable {
            id: flick
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentHeight: projectsColumn.implicitHeight + Theme.space4
            boundsBehavior: Flickable.StopAtBounds
            interactive: sidebar.dragFrom < 0
            TouchFlick.enabled: true
            T.ScrollBar.vertical: BScrollBar {}

            ColumnLayout {
                id: projectsColumn
                width: flick.width
                spacing: Theme.space1

                // What the desktop app on the user's other computers is doing (its threads, its
                // conversations with agents): followed and steered from here.
                ChatComputerList {
                    Layout.fillWidth: true
                    current: sidebar.routePath
                    onOpened: (path) => sidebar.go({ view: "code", path: path })
                }

                // Clones running, and those that failed (CodeProjects.clones).
                Repeater {
                    model: Workspace.projects.clones
                    delegate: CodeCloneRow {}
                }

                Repeater {
                    id: projectsRepeater
                    model: Workspace.projects.list
                    delegate: CodeProjectSection {
                        id: projectSection
                        open: !sidebar.folded(projectSection.projectId)
                        filter: sidebar.filter
                        sortBy: Workspace.projects.threadSort
                        routePath: sidebar.routePath
                        newShown: sidebar.routeNew && sidebar.routeProject === projectSection.projectId
                        dragging: sidebar.dragFrom === projectSection.index
                        onNavigate: (route) => sidebar.go(route)
                        onToggleRequested: sidebar.toggle(projectSection.projectId)
                        onMenuRequested: (item, x, y) => projectMenu.openFor(projectSection.projectId, projectSection.projectName,
                                                                             projectSection.projectPath, projectSection.projectPinned,
                                                                             projectSection.index, item, x, y)
                        onRenameThread: (path, title) => renameDialog.openFor(path, title)
                        onSnoozeRequested: (path, title) => snoozeDialog.openFor(path, title)
                        onDeleteThread: (path, title, worktree, branch, project) => sidebar.deleteThread(path, title, worktree, branch, project)
                        onCopyRequested: (text, what) => {
                            if (pageTools.copyText(text))
                                AppController.toast(what, "success")
                        }
                        onDragStarted: sidebar.dragStarted(projectSection.index)
                        onDragMoved: (dy) => sidebar.dragMoved(projectSection.index, dy)
                        onDragEnded: sidebar.dragEnded(projectSection.index)
                    }
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
                    BButton {
                        objectName: "codeCloneEmpty"
                        visible: Workspace.projects.gitAvailable
                        size: "small"
                        variant: "ghost"
                        iconName: "download"
                        text: qsTr("Clone a repository…")
                        onClicked: cloner.start()
                    }
                }
            }
            // Where a dragged project goes.
            Rectangle {
                objectName: "codeProjectDrop"
                visible: sidebar.dragFrom >= 0 && sidebar.dropAt >= 0 && sidebar.dropAt !== sidebar.dragFrom
                         && sidebar.dropAt !== sidebar.dragFrom + 1
                x: Theme.space3
                width: flick.width - 2 * Theme.space3
                height: 2
                radius: 1
                color: Theme.accent
                y: {
                    if (sidebar.dropAt < 0)
                        return 0
                    const s = sidebar.section(Math.min(sidebar.dropAt, projectsRepeater.count - 1))
                    if (s === null)
                        return 0
                    return sidebar.dropAt >= projectsRepeater.count ? s.y + s.height : s.y - 1
                }
            }
        }
    }

    BMenu {
        id: sortMenu
        objectName: "codeThreadSortMenu"
        BMenuItem {
            objectName: "codeSortActivity"
            text: qsTr("Last activity first")
            checkable: true
            checked: Workspace.projects.threadSort !== "created"
            onTriggered: Workspace.projects.threadSort = "activity"
        }
        BMenuItem {
            objectName: "codeSortCreated"
            text: qsTr("Newest first")
            checkable: true
            checked: Workspace.projects.threadSort === "created"
            onTriggered: Workspace.projects.threadSort = "created"
        }
    }

    BMenu {
        id: addMenu
        objectName: "codeAddProjectMenu"
        BMenuItem {
            objectName: "codeAddFolder"
            text: qsTr("Open a folder…")
            iconName: "folder-open"
            onTriggered: folderPicker.open()
        }
        BMenuItem {
            objectName: "codeAddClone"
            text: qsTr("Clone a repository…")
            iconName: "download"
            enabled: Workspace.projects.gitAvailable
            onTriggered: cloner.start()
        }
    }

    CodeCloneDialog {
        id: cloner
        onAdded: (project) => sidebar.go({ view: "code", new: true, project: project })
    }

    // ---- a project's menu ------------------------------------------------------------------------------
    BMenu {
        id: projectMenu
        objectName: "codeProjectMenu"
        property string projectId
        property string projectName
        property string projectPath
        property bool pinned: false
        property int at: -1
        function openFor(id: string, name: string, path: string, pinned: bool, at: int, item: Item, x: real, y: real): void {
            projectMenu.projectId = id
            projectMenu.projectName = name
            projectMenu.projectPath = path
            projectMenu.pinned = pinned
            projectMenu.at = at
            projectMenu.popup(item, x, y)
        }
        BMenuItem {
            objectName: "codeProjectNewThread"
            text: qsTr("New thread")
            iconName: "plus"
            onTriggered: sidebar.go({ view: "code", new: true, project: projectMenu.projectId })
        }
        BMenuItem {
            objectName: "codeProjectNewLocalThread"
            text: qsTr("New thread in the project's folder")
            iconName: "hard-drive"
            onTriggered: sidebar.go({ view: "code", new: true, project: projectMenu.projectId, env: "local" })
        }
        BMenuSeparator {}
        BMenuItem {
            objectName: "codeProjectEditor"
            text: qsTr("Open in editor")
            iconName: "code"
            onTriggered: {
                if (!Workspace.projects.openInEditor(projectMenu.projectPath, -1))
                    AppController.toast(qsTr("The editor could not be started: choose one in Settings → Extensions → Code mode."), "error")
            }
        }
        BMenuItem {
            objectName: "codeProjectFolder"
            text: qsTr("Show the folder")
            iconName: "folder-open"
            onTriggered: Workspace.projects.openFolder(projectMenu.projectPath)
        }
        BMenuItem {
            objectName: "codeProjectCopyPath"
            text: qsTr("Copy path")
            iconName: "copy"
            onTriggered: {
                if (pageTools.copyText(projectMenu.projectPath))
                    AppController.toast(qsTr("Path copied"), "success")
            }
        }
        BMenuSeparator {}
        BMenuItem {
            objectName: "codeProjectPin"
            text: projectMenu.pinned ? qsTr("Unpin") : qsTr("Pin")
            iconName: "pin"
            onTriggered: Workspace.projects.setPinned(projectMenu.projectId, !projectMenu.pinned)
        }
        BMenuItem {
            objectName: "codeProjectMoveUp"
            text: qsTr("Move up")
            iconName: "arrow-up"
            enabled: projectMenu.at > 0
            onTriggered: Workspace.projects.move(projectMenu.projectId, projectMenu.at - 1)
        }
        BMenuItem {
            objectName: "codeProjectMoveDown"
            text: qsTr("Move down")
            iconName: "arrow-down"
            enabled: projectMenu.at >= 0 && projectMenu.at < Workspace.projects.count - 1
            onTriggered: Workspace.projects.move(projectMenu.projectId, projectMenu.at + 1)
        }
        BMenuItem {
            objectName: "codeProjectSettings"
            text: qsTr("Project settings…")
            iconName: "sliders-horizontal"
            onTriggered: projectDialog.openFor(projectMenu.projectId)
        }
        BMenuSeparator {}
        BMenuItem {
            objectName: "codeProjectRemove"
            text: qsTr("Remove from the list")
            iconName: "x"
            danger: true
            onTriggered: removeDialog.openFor(projectMenu.projectId, projectMenu.projectName)
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

    // ---- a thread renamed ------------------------------------------------------------------------------
    BDialog {
        id: renameDialog
        objectName: "codeRenameThreadDialog"
        property string path
        function openFor(path: string, title: string): void {
            renameDialog.path = path
            renameField.text = title
            renameDialog.open()
            renameField.selectAll()
        }
        parent: T.Overlay.overlay
        title: qsTr("Rename the thread")
        iconName: "pencil"
        acceptText: qsTr("Rename")
        acceptEnabled: Js.trim(renameField.text) !== ""
        initialFocusItem: renameField
        onAccepted: {
            const current = renameDialog.path === sidebar.routePath
            const renamed = Workspace.chats.rename(renameDialog.path, Js.trim(renameField.text))
            if (renamed === "")
                AppController.toast(qsTr("The thread could not be renamed: another one has that name."), "error")
            else if (current)
                AppController.replaceRoute({ view: "code", path: renamed })
        }
        BTextField {
            id: renameField
            objectName: "codeRenameThreadField"
            Layout.fillWidth: true
            accessibleName: qsTr("The thread's name")
            onAccepted: renameDialog.tryAccept()
        }
    }

    // ---- a thread snoozed until a time typed ("jutro o 18", "in 2 hours", "2026-12-01 9:00") ----------
    BDialog {
        id: snoozeDialog
        objectName: "codeSnoozeDialog"
        property string path
        function openFor(path: string, title: string): void {
            snoozeDialog.path = path
            snoozeDialog.threadTitle = title
            snoozeField.text = ""
            snoozeDialog.open()
        }
        property string threadTitle
        readonly property string ends: Workspace.codeThreads.snoozeLabel(snoozeField.text)
        parent: T.Overlay.overlay
        title: qsTr("Snooze “%1”").arg(snoozeDialog.threadTitle)
        iconName: "alarm-clock"
        acceptText: qsTr("Snooze")
        acceptEnabled: snoozeDialog.ends !== ""
        initialFocusItem: snoozeField
        onAccepted: {
            const why = Workspace.codeThreads.snooze(snoozeDialog.path, snoozeField.text)
            if (why !== "")
                AppController.toast(why, "error")
            else
                AppController.toastUndoChatMark(qsTr("Thread snoozed"), qsTr("Undo"), snoozeDialog.path, "snoozed", true)
        }
        BTextField {
            id: snoozeField
            objectName: "codeSnoozeField"
            Layout.fillWidth: true
            accessibleName: qsTr("Until when")
            placeholderText: qsTr("tomorrow at 9, in 2 hours, 2026-12-01 18:00")
            onAccepted: snoozeDialog.tryAccept()
        }
        BText {
            Layout.fillWidth: true
            text: snoozeDialog.ends !== "" ? qsTr("Wakes %1").arg(snoozeDialog.ends)
                  : snoozeField.text !== "" ? qsTr("No time ahead in that.") : qsTr("It comes back earlier when it asks something or fails.")
            variant: "small"
            color: Theme.fgMuted
            wrapMode: Text.Wrap
        }
    }

    // ---- a thread with a worktree of its own deleted: the worktree too? --------------------------------
    BConfirmDialog {
        id: deleteDialog
        objectName: "codeDeleteThreadDialog"
        property string path
        property string worktree
        property string branch
        property string project
        property string threadTitle
        /// The worktree's uncommitted changes (-1 not known yet).
        property int changes: -1
        property bool checking: false
        function prepare(path: string, title: string, worktree: string, branch: string, project: string): void {
            deleteDialog.path = path
            deleteDialog.threadTitle = title
            deleteDialog.worktree = worktree
            deleteDialog.branch = branch
            deleteDialog.project = project
            deleteDialog.changes = -1
            deleteDialog.checking = Workspace.projects.checkWorktree(worktree)
            if (!deleteDialog.checking)
                deleteDialog.show()
        }
        function show(): void {
            // (The device deletes them by itself: without asking when nothing would be lost.)
            if (Workspace.projects.deletedWorktrees === "delete" && deleteDialog.changes === 0) {
                sidebar.trashThread(deleteDialog.path, deleteDialog.project, deleteDialog.worktree, deleteDialog.branch, true, false)
                return
            }
            deleteDialog.subject = deleteDialog.threadTitle
            deleteDialog.checked = deleteDialog.changes === 0
            deleteDialog.open()
        }
        Connections {
            target: Workspace.projects
            function onWorktreeChecked(folder: string, changes: int, branch: string, error: string) {
                if (!deleteDialog.checking || folder !== deleteDialog.worktree)
                    return
                deleteDialog.checking = false
                deleteDialog.changes = changes
                deleteDialog.show()
            }
        }
        parent: T.Overlay.overlay
        title: qsTr("Move the thread to the trash?")
        subjectIcon: "message-square"
        message: qsTr("It works in a worktree of its own that no other thread uses: %1. Its branch %2 stays.")
                 .arg(deleteDialog.worktree).arg(deleteDialog.branch)
        detail: deleteDialog.changes > 0
                ? qsTr("The worktree has uncommitted changes in %n file(s): deleting it loses them.", "", deleteDialog.changes)
                : deleteDialog.changes === 0 ? qsTr("The worktree has no uncommitted changes.")
                : qsTr("Whether the worktree has uncommitted changes is not known.")
        checkText: qsTr("Delete the worktree too")
        confirmText: qsTr("Move to trash")
        destructive: true
        onAccepted: sidebar.trashThread(deleteDialog.path, deleteDialog.project, deleteDialog.worktree, deleteDialog.branch,
                                        deleteDialog.checked, deleteDialog.changes !== 0)
    }
}
