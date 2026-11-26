pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// TerminalPanel — the Code mode's terminal panel (docs/CODE-TERMINAL.md "The panel"): the terminals
// of one thread (TerminalRegistry.thread(threadKey, folder)) — tabs, each up to four terminals side
// by side or stacked (TerminalPane), the shown tab's filling the panel —, "+" for another shell,
// split right / split down, a find bar over the scrollback, a context menu (Add to chat, Copy, Paste,
// Select all, Find…, Jump to latest, Clear, splits, Close). Another thread shown: its own terminals;
// those of the thread before stay in the registry, running, until it is shown again. run() is the
// project's "scripts" button: a new tab running a command through the user's shell. Where there is
// no pseudo-terminal (Windows, Android, the browser) the panel says "No terminal on this device".
//
//   TerminalPanel { threadKey: path !== "" ? path : folder; folder: thread.folder; dark: Theme.dark }
//   panel.run("npm run dev", qsTr("Dev server"))
//
// A FocusScope: giving the panel the focus gives it to the focused terminal of the shown tab. A new
// terminal opened by the user (newTerminal(), run(), split(), "+") takes the focus; the shell opened
// by itself when a thread without terminals is shown (`openShellOnStart`, while `shown`) does not,
// and none opens by itself in a thread whose last terminal the user closed. Closing a terminal in
// which a program runs asks first.
//
// objectNames: terminalPanel, terminalTabs, terminalTab_<n> (TerminalTab's), terminalNew,
// terminalSplitRight, terminalSplitDown, terminalFindButton, terminalPanes, terminalPane_<n>
// (TerminalPane's), terminalFindBar (TerminalFindBar's), terminalMenu (terminalMenuAddToChat,
// terminalMenuCopy, terminalMenuPaste, terminalMenuSelectAll, terminalMenuFind,
// terminalMenuJumpLatest, terminalMenuClear, terminalMenuSplitRight, terminalMenuSplitDown,
// terminalMenuClose), terminalCloseConfirm, terminalEmpty, terminalUnavailable.
FocusScope {
    id: root
    objectName: "terminalPanel"

    /// Whose terminals: a thread's page path, a new thread's folder ("": none).
    property string threadKey
    /// Where new terminals start (the thread's folder; "": the home folder).
    property string folder
    /// The terminal's colours: the dark palette, else the light one.
    property bool dark: Theme.dark
    /// A shell opens by itself when the panel shows a thread without terminals.
    property bool openShellOnStart: true
    /// The panel is on screen (a shell opens by itself only then).
    property bool shown: true
    /// The thread's terminals (TerminalRegistry's).
    property TerminalThread thread: null
    readonly property TerminalGroup group: root.thread !== null ? root.thread.current : null
    readonly property int count: root.thread !== null ? root.thread.count : 0
    readonly property int currentIndex: root.thread !== null ? root.thread.currentIndex : -1
    readonly property bool available: TerminalRegistry.available
    /// The focused terminal of the shown tab (null: none).
    readonly property TerminalSession currentSession: root.thread !== null ? root.thread.currentSession : null
    readonly property bool findOpen: findBar.visible
    /// Chords of the panel's commands (Settings → Keyboard shortcuts): theirs inside the terminal too.
    readonly property list<string> appShortcuts: [
        ShortcutMap.sequences["terminal.toggle"] || "",
        ShortcutMap.sequences["terminal.new"] || "",
        ShortcutMap.sequences["terminal.splitRight"] || "",
        ShortcutMap.sequences["terminal.splitDown"] || "",
        ShortcutMap.sequences["terminal.close"] || "",
        ShortcutMap.sequences["terminal.next"] || "",
        ShortcutMap.sequences["terminal.previous"] || ""
    ]

    /// Ctrl+click on a web address a program printed (OSC 8 or in the text): the owner decides
    /// whether to open it.
    signal linkActivated(string url)
    /// Ctrl+click on the path of an existing file (line and column: 0 when none given).
    signal fileActivated(string path, int line, int column)
    /// "Add to chat": the selection, or the rows in view, as a fenced block for the composer.
    signal addToChat(string text)

    /// Shows the terminals of `threadKey` (made when it has none).
    function attach(): void {
        root.thread = root.threadKey !== "" ? TerminalRegistry.thread(root.threadKey, root.folder) : null
        TerminalRegistry.shown(root.threadKey)
        root.closeFind()
        openLater.restart()
    }
    onThreadKeyChanged: root.attach()
    onFolderChanged: {
        if (root.thread !== null && root.folder !== "")
            root.thread.folder = root.folder
    }
    onShownChanged: openLater.restart()
    Component.onCompleted: root.attach()

    /// A new tab running `command` in `folder` through the user's shell (`$SHELL -lc command`):
    /// the dev server, the tests. `label` names the tab until the program sets a title.
    function run(command: string, label: string): void {
        root.openTab(command, label, true)
    }
    /// A new tab with the user's shell.
    function newTerminal(): void {
        root.openTab("", "", true)
    }
    /// A new tab running `command` ("": the shell), shown and started; `takeFocus`: the keyboard
    /// goes to it.
    function openTab(command: string, label: string, takeFocus: bool): void {
        if (!root.available || root.thread === null)
            return
        const session = root.thread.openTab(command, label)
        if (session === null) {
            root.sayError()
            return
        }
        if (takeFocus)
            focusLater.restart()
    }
    /// A shell beside (Qt.Horizontal) or under (Qt.Vertical) the focused terminal.
    function split(orientation: int): void {
        if (!root.available || root.thread === null)
            return
        const session = root.thread.split(orientation === Qt.Vertical ? Qt.Vertical : Qt.Horizontal)
        if (session === null) {
            root.sayError()
            return
        }
        focusLater.restart()
    }
    function sayError(): void {
        if (root.thread !== null && root.thread.error !== "")
            AppController.toast(root.thread.error, "warning")
    }
    /// Shows tab `index`.
    function select(index: int): void {
        if (root.thread !== null)
            root.thread.currentIndex = index
    }
    /// The focus `delta` terminals on: the shown tab's panes, then the next tab's.
    function focusNext(delta: int): void {
        if (root.thread === null)
            return
        root.thread.focusNext(delta)
        focusLater.restart()
    }
    /// The keyboard to the focused terminal of the shown tab.
    function focusTerminal(): void {
        const pane = root.paneAt(root.group !== null ? root.group.focusIndex : -1)
        if (pane !== null)
            pane.focusTerminal()
        else
            root.forceActiveFocus()
    }
    Later {
        id: focusLater
        onTriggered: root.focusTerminal()
    }
    function paneAt(index: int): TerminalPane {
        // (panes.count: bindings that ask are evaluated again once the Repeater made its items.)
        return index >= 0 && index < panes.count ? panes.itemAt(index) as TerminalPane : null
    }
    /// The focused pane's view (null: none).
    function currentView(): TerminalView {
        const pane = root.paneAt(root.group !== null ? root.group.focusIndex : -1)
        return pane !== null ? pane.view : null
    }

    // ---- Closing (a program running in it: asked first) ------------------------------------------
    property TerminalSession pendingClose: null
    property int pendingTab: -1
    /// Closes tab `index` (its programs are hung up on).
    function closeTab(index: int): void {
        if (root.thread === null)
            return
        const group = root.thread.group(index) as TerminalGroup
        if (group === null)
            return
        if (group.busy) {
            root.pendingClose = null
            root.pendingTab = index
            closeConfirm.subject = group.title
            closeConfirm.open()
            return
        }
        root.thread.closeTab(index, true)
    }
    /// Closes terminal `session` (the focused one: closeCurrent()).
    function closeSession(session: TerminalSession): void {
        if (root.thread === null || session === null)
            return
        if (session.busy) {
            root.pendingClose = session
            root.pendingTab = -1
            closeConfirm.subject = session.title
            closeConfirm.open()
            return
        }
        root.thread.closeSession(session, true)
        focusLater.restart()
    }
    function closeCurrent(): void {
        root.closeSession(root.currentSession)
    }
    /// Runs the focused terminal's program again.
    function restart(): void {
        const pane = root.paneAt(root.group !== null ? root.group.focusIndex : -1)
        if (pane !== null)
            pane.restart()
    }

    // ---- Find ------------------------------------------------------------------------------------
    function openFind(): void {
        findBar.visible = true
        findBar.focusField()
        findBar.search()
    }
    function closeFind(): void {
        if (!findBar.visible)
            return
        findBar.visible = false
        const view = root.currentView()
        if (view !== null)
            view.clearFind()
    }

    // ---- The context menu ------------------------------------------------------------------------
    property TerminalPane menuPane: null
    function openMenu(pane: TerminalPane, x: real, y: real): void {
        root.menuPane = pane
        contextMenu.popup(pane, x, y)
    }
    function menuView(): TerminalView {
        return root.menuPane !== null ? root.menuPane.view : null
    }

    function openShellIfWanted(): void {
        if (root.openShellOnStart && root.shown && root.available && root.thread !== null && root.thread.count === 0
                && root.thread.autoOpen)
            root.openTab("", "", false)
    }
    Later {
        id: openLater
        onTriggered: root.openShellIfWanted()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        visible: root.available

        // The tabs and the panel's buttons.
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: Theme.controlHeightSm + Theme.space2
            color: Theme.bg

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.space1
                anchors.rightMargin: Theme.space1
                spacing: Theme.space1

                ListView {
                    id: tabs
                    objectName: "terminalTabs"
                    Layout.fillHeight: true
                    Layout.fillWidth: true
                    Layout.maximumWidth: tabs.contentWidth
                    orientation: ListView.Horizontal
                    spacing: Theme.space1
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: root.thread
                    currentIndex: root.currentIndex
                    Accessible.role: Accessible.PageTabList
                    Accessible.name: qsTr("Terminals")

                    delegate: TerminalTab {
                        id: tabDelegate
                        y: (tabs.height - tabDelegate.height) / 2
                        current: tabDelegate.index === root.currentIndex
                        onClicked: {
                            root.select(tabDelegate.index)
                            focusLater.restart()
                        }
                        onCloseRequested: root.closeTab(tabDelegate.index)
                    }
                }

                BIconButton {
                    objectName: "terminalNew"
                    Layout.alignment: Qt.AlignVCenter
                    iconName: "plus"
                    size: "small"
                    label: qsTr("New terminal")
                    shortcut: ShortcutMap.sequences["terminal.new"] || ""
                    onClicked: root.newTerminal()
                }
                BIconButton {
                    objectName: "terminalSplitRight"
                    Layout.alignment: Qt.AlignVCenter
                    iconName: "columns-2"
                    size: "small"
                    label: qsTr("Split right")
                    shortcut: ShortcutMap.sequences["terminal.splitRight"] || ""
                    enabled: root.group === null || root.group.canSplit
                    onClicked: root.split(Qt.Horizontal)
                }
                BIconButton {
                    objectName: "terminalSplitDown"
                    Layout.alignment: Qt.AlignVCenter
                    iconName: "rows-2"
                    size: "small"
                    label: qsTr("Split down")
                    shortcut: ShortcutMap.sequences["terminal.splitDown"] || ""
                    enabled: root.group === null || root.group.canSplit
                    onClicked: root.split(Qt.Vertical)
                }

                Item {
                    Layout.fillWidth: true
                }

                BIconButton {
                    objectName: "terminalFindButton"
                    Layout.alignment: Qt.AlignVCenter
                    iconName: "search"
                    size: "small"
                    label: qsTr("Find in terminal")
                    shortcut: "Ctrl+Shift+F"
                    checked: findBar.visible
                    enabled: root.group !== null
                    onClicked: {
                        if (findBar.visible) {
                            root.closeFind()
                            root.focusTerminal()
                        } else {
                            root.openFind()
                        }
                    }
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.border
            }
        }

        // The shown tab's terminals.
        Item {
            id: content
            Layout.fillWidth: true
            Layout.fillHeight: true

            Rectangle {
                id: paneArea
                objectName: "terminalPanes"
                anchors.fill: parent
                visible: root.group !== null && root.group.count > 0
                // (The 1-pixel gaps between the panes.)
                color: Theme.border
                readonly property int paneCount: root.group !== null ? root.group.count : 0
                readonly property bool sideBySide: root.group === null || root.group.orientation === Qt.Horizontal
                readonly property real gap: 1

                Repeater {
                    id: panes
                    model: root.group
                    delegate: TerminalPane {
                        id: paneDelegate
                        readonly property int n: Math.max(1, paneArea.paneCount)
                        readonly property real share: ((paneArea.sideBySide ? paneArea.width : paneArea.height)
                                                       - paneArea.gap * (paneDelegate.n - 1)) / paneDelegate.n
                        x: paneArea.sideBySide ? paneDelegate.index * (paneDelegate.share + paneArea.gap) : 0
                        y: paneArea.sideBySide ? 0 : paneDelegate.index * (paneDelegate.share + paneArea.gap)
                        width: paneArea.sideBySide ? paneDelegate.share : paneArea.width
                        height: paneArea.sideBySide ? paneArea.height : paneDelegate.share
                        dark: root.dark
                        split: paneArea.paneCount > 1
                        focusedPane: root.group !== null && paneDelegate.index === root.group.focusIndex
                        focus: paneDelegate.focusedPane
                        appShortcuts: root.appShortcuts

                        Rectangle {
                            anchors.fill: parent
                            z: -1
                            color: paneDelegate.view.background
                        }

                        onFocusRequested: {
                            if (root.group !== null)
                                root.group.focusIndex = paneDelegate.index
                        }
                        onLinkActivated: (url) => root.linkActivated(url)
                        onFileActivated: (path, line, column) => root.fileActivated(path, line, column)
                        onMenuRequested: (x, y) => root.openMenu(paneDelegate, x, y)
                        onFindRequested: root.openFind()
                        onCloseRequested: root.closeSession(paneDelegate.session)
                    }
                }
            }

            TerminalFindBar {
                id: findBar
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.topMargin: Theme.space2
                anchors.rightMargin: Theme.space2 + 10
                width: Math.min(implicitWidth, parent.width - 2 * Theme.space2)
                visible: false
                z: 2
                view: root.currentView()
                onCloseRequested: {
                    root.closeFind()
                    root.focusTerminal()
                }
            }

            BEmptyState {
                objectName: "terminalEmpty"
                anchors.centerIn: parent
                visible: root.thread !== null && root.thread.count === 0
                compact: true
                iconName: "terminal"
                title: qsTr("No terminals open")
                description: qsTr("A terminal runs your shell in the thread's folder.")
                actionText: qsTr("New terminal")
                actionIcon: "plus"
                onActionTriggered: root.newTerminal()
            }
        }
    }

    BEmptyState {
        objectName: "terminalUnavailable"
        anchors.centerIn: parent
        visible: !root.available
        iconName: "terminal"
        title: qsTr("No terminal on this device")
        description: qsTr("Terminals run on Linux and macOS.")
    }

    BMenu {
        id: contextMenu
        objectName: "terminalMenu"
        BMenuItem {
            objectName: "terminalMenuAddToChat"
            text: qsTr("Add to chat")
            iconName: "message-square"
            onTriggered: {
                const view = root.menuView()
                const text = view !== null ? view.chatText() : ""
                if (text !== "")
                    root.addToChat(text)
            }
        }
        BMenuSeparator {}
        BMenuItem {
            objectName: "terminalMenuCopy"
            text: qsTr("Copy")
            iconName: "copy"
            shortcut: "Ctrl+Shift+C"
            enabled: root.menuView() !== null && root.menuView().hasSelection
            onTriggered: {
                const view = root.menuView()
                if (view !== null)
                    view.copy()
            }
        }
        BMenuItem {
            objectName: "terminalMenuPaste"
            text: qsTr("Paste")
            iconName: "clipboard-paste"
            shortcut: "Ctrl+Shift+V"
            enabled: root.menuPane !== null && root.menuPane.running
            onTriggered: {
                const view = root.menuView()
                if (view !== null)
                    view.paste()
            }
        }
        BMenuItem {
            objectName: "terminalMenuSelectAll"
            text: qsTr("Select all")
            iconName: "text-select"
            shortcut: "Ctrl+Shift+A"
            onTriggered: {
                const view = root.menuView()
                if (view !== null)
                    view.selectAll()
            }
        }
        BMenuSeparator {}
        BMenuItem {
            objectName: "terminalMenuFind"
            text: qsTr("Find…")
            iconName: "search"
            shortcut: "Ctrl+Shift+F"
            onTriggered: root.openFind()
        }
        BMenuItem {
            objectName: "terminalMenuJumpLatest"
            text: qsTr("Jump to latest")
            iconName: "arrow-down"
            shortcut: "Ctrl+Shift+End"
            enabled: root.menuView() !== null && root.menuView().scrollOffset > 0
            onTriggered: {
                const view = root.menuView()
                if (view !== null)
                    view.scrollToBottom()
            }
        }
        BMenuItem {
            objectName: "terminalMenuClear"
            text: qsTr("Clear")
            iconName: "eraser"
            onTriggered: {
                const view = root.menuView()
                if (view !== null)
                    view.clear()
            }
        }
        BMenuSeparator {}
        BMenuItem {
            objectName: "terminalMenuSplitRight"
            text: qsTr("Split right")
            iconName: "columns-2"
            shortcut: ShortcutMap.sequences["terminal.splitRight"] || ""
            enabled: root.group !== null && root.group.canSplit
            onTriggered: root.split(Qt.Horizontal)
        }
        BMenuItem {
            objectName: "terminalMenuSplitDown"
            text: qsTr("Split down")
            iconName: "rows-2"
            shortcut: ShortcutMap.sequences["terminal.splitDown"] || ""
            enabled: root.group !== null && root.group.canSplit
            onTriggered: root.split(Qt.Vertical)
        }
        BMenuSeparator {}
        BMenuItem {
            objectName: "terminalMenuClose"
            text: qsTr("Close terminal")
            iconName: "x"
            shortcut: ShortcutMap.sequences["terminal.close"] || ""
            onTriggered: {
                if (root.menuPane !== null)
                    root.closeSession(root.menuPane.session)
            }
        }
    }

    BConfirmDialog {
        id: closeConfirm
        objectName: "terminalCloseConfirm"
        parent: T.Overlay.overlay
        title: qsTr("Close the terminal?")
        subjectIcon: "terminal"
        message: qsTr("A program is still running in it. Closing the terminal stops it.")
        confirmText: qsTr("Close")
        destructive: true
        onAccepted: {
            if (root.thread === null)
                return
            if (root.pendingClose !== null)
                root.thread.closeSession(root.pendingClose, true)
            else if (root.pendingTab >= 0)
                root.thread.closeTab(root.pendingTab, true)
            root.pendingClose = null
            root.pendingTab = -1
            focusLater.restart()
        }
        onRejected: {
            root.pendingClose = null
            root.pendingTab = -1
        }
    }
}
