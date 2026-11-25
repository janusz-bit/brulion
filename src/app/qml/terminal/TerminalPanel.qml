pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// TerminalPanel — the Code mode's terminal panel (docs/CODE-TERMINAL.md "The panel"): tabs of
// terminals started in `folder` (TerminalSessionModel), the current one's TerminalView filling the
// rest, "+" for another shell, × to close one (its program gets SIGHUP). A terminal whose program
// ended says "Process exited with code N" under its output, with Restart. run() is the project's
// "scripts" button: a new tab running a command through the user's shell. Where there is no
// pseudo-terminal (Windows, Android, the browser) the panel says "No terminal on this device".
//
//   TerminalPanel { folder: project.folder; dark: Theme.dark }
//   panel.run("npm run dev", qsTr("Dev server"))
//
// A FocusScope: giving the panel the focus gives it to the terminal shown. A new tab opened by the
// user (newTerminal(), run(), "+") takes the focus; the shell opened by itself at the start does not.
//
// objectNames: terminalPanel, terminalTabs, terminalTab_<n> (terminalTabTitle_<n>,
// terminalTabClose_<n>), terminalNew, terminalView, terminalEmpty (+ its button), terminalExited
// (terminalExitedText, terminalRestart), terminalUnavailable.
FocusScope {
    id: root
    objectName: "terminalPanel"

    /// Where new terminals start (the project's folder; "": the home folder).
    property string folder
    /// The terminal's colours: the dark palette, else the light one.
    property bool dark: Theme.dark
    /// A shell opens by itself when the panel starts without terminals.
    property bool openShellOnStart: true
    /// The tab shown (-1: none).
    property int currentIndex: -1
    readonly property int count: sessions.count
    readonly property bool available: sessions.available
    /// The terminal shown (null: none).
    readonly property TerminalSession currentSession: view.session

    /// Ctrl+click on a link a program printed (OSC 8): the owner decides whether to open it.
    signal linkActivated(string url)

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
        if (!root.available)
            return
        const index = sessions.add(root.folder, command, label)
        root.select(index)
        if (view.session !== null)
            view.session.start()
        if (takeFocus)
            view.forceActiveFocus()
    }

    /// Shows tab `index`.
    function select(index: int): void {
        root.currentIndex = index
        view.session = sessions.at(index)
    }

    /// Closes tab `index`: its program is hung up on; the tab beside it is shown.
    function closeTab(index: int): void {
        if (index < 0 || index >= sessions.count)
            return
        const current = root.currentIndex
        if (index === current)
            view.session = null
        sessions.remove(index)
        if (sessions.count === 0) {
            root.currentIndex = -1
            return
        }
        let next = current
        if (index < current)
            next = current - 1
        else if (index === current)
            next = Math.min(index, sessions.count - 1)
        root.select(next)
    }

    /// Runs the shown terminal's program again.
    function restart(): void {
        if (view.session === null)
            return
        view.session.start()
        view.forceActiveFocus()
    }

    function exitText(): string {
        const session = view.session
        if (session === null)
            return ""
        if (session.errorString !== "")
            return session.errorString
        if (!session.hasExited)
            return ""
        return qsTr("Process exited with code %1").arg(session.exitCode)
    }

    TerminalSessionModel {
        id: sessions
    }

    Later {
        id: openLater
        onTriggered: {
            if (root.openShellOnStart && root.available && sessions.count === 0)
                root.openTab("", "", false)
        }
    }

    Component.onCompleted: openLater.restart()

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        visible: root.available

        // The tabs.
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
                    model: sessions
                    currentIndex: root.currentIndex
                    Accessible.role: Accessible.PageTabList
                    Accessible.name: qsTr("Terminals")

                    delegate: TerminalTab {
                        id: tabDelegate
                        y: (tabs.height - tabDelegate.height) / 2
                        current: tabDelegate.index === root.currentIndex
                        onClicked: {
                            root.select(tabDelegate.index)
                            view.forceActiveFocus()
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
                    onClicked: root.newTerminal()
                }

                Item {
                    Layout.fillWidth: true
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.border
            }
        }

        // The terminal.
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            TerminalView {
                id: view
                objectName: "terminalView"
                anchors.fill: parent
                visible: view.session !== null
                dark: root.dark
                font.family: Theme.fontMono
                font.pixelSize: Theme.textBase
                padding: Theme.space2
                focus: true

                Accessible.role: Accessible.Terminal
                Accessible.name: view.session !== null ? view.session.title : qsTr("Terminal")
                Accessible.description: view.accessibleText

                onLinkActivated: (url) => root.linkActivated(url)
            }

            BEmptyState {
                objectName: "terminalEmpty"
                anchors.centerIn: parent
                visible: sessions.count === 0
                compact: true
                iconName: "terminal"
                title: qsTr("No terminals open")
                description: qsTr("A terminal runs your shell in the project's folder.")
                actionText: qsTr("New terminal")
                actionIcon: "plus"
                onActionTriggered: root.newTerminal()
            }
        }

        // The program ended (or could not start).
        Rectangle {
            objectName: "terminalExited"
            Layout.fillWidth: true
            visible: root.exitText() !== ""
            implicitHeight: exitedRow.implicitHeight + 2 * Theme.space2
            color: Theme.bgElevated

            Rectangle {
                width: parent.width
                height: 1
                color: Theme.border
            }

            RowLayout {
                id: exitedRow
                anchors.fill: parent
                anchors.leftMargin: Theme.space3
                anchors.rightMargin: Theme.space2
                spacing: Theme.space2

                BIcon {
                    Layout.alignment: Qt.AlignVCenter
                    name: "circle-x"
                    size: Theme.iconSizeSm
                    color: Theme.fgMuted
                }
                BText {
                    objectName: "terminalExitedText"
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    variant: "muted"
                    text: root.exitText()
                    elide: Text.ElideRight
                }
                BButton {
                    objectName: "terminalRestart"
                    Layout.alignment: Qt.AlignVCenter
                    text: qsTr("Restart")
                    iconName: "rotate-ccw"
                    size: "small"
                    onClicked: root.restart()
                }
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
}
