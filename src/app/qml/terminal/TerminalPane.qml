pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// TerminalPane — one terminal of a TerminalPanel tab (docs/CODE-TERMINAL.md "The panel"): its
// TerminalView, a scroll bar over the scrollback, "Jump to latest" while scrolled back, and — once
// its program ended, or could not start — "Process exited with code N" with Restart and Close. In a
// split tab the focused pane is marked by a line in the accent colour at its top. The panel's
// Repeater makes one per TerminalGroup row and keeps the group's focus with the pane the keyboard
// is in (focusRequested()).
//
//   TerminalPane { session: …; focusedPane: index === group.focusIndex; split: group.count > 1 }
//
// objectNames: terminalPane_<n>, terminalView (terminalView_<n> in a split), terminalScrollBar_<n>,
// terminalJumpLatest_<n>, terminalExited_<n> (terminalExitedText_<n>, terminalRestart_<n>,
// terminalCloseExited_<n>).
FocusScope {
    id: pane
    objectName: "terminalPane_" + pane.index

    required property TerminalSession session
    required property int index
    /// The group's focused terminal (the keyboard goes to it).
    property bool focusedPane: false
    /// Other terminals beside it in its tab.
    property bool split: false
    property bool dark: Theme.dark
    /// Key chords the app's shortcuts keep while a program runs (TerminalView.appShortcuts).
    property list<string> appShortcuts
    readonly property TerminalView view: view
    /// Its program runs (what it is typed into: the menu's Paste).
    readonly property bool running: pane.session !== null && pane.session.running

    signal focusRequested()
    signal linkActivated(string url)
    signal fileActivated(string path, int line, int column)
    /// The context menu at (x, y) of the pane.
    signal menuRequested(real x, real y)
    signal findRequested()
    signal closeRequested()

    function exitText(): string {
        if (pane.session === null)
            return ""
        if (pane.session.errorString !== "")
            return pane.session.errorString
        if (!pane.session.hasExited)
            return ""
        return qsTr("Process exited with code %1").arg(pane.session.exitCode)
    }
    function restart(): void {
        if (pane.session === null)
            return
        pane.session.start()
        view.forceActiveFocus()
    }
    function focusTerminal(): void {
        view.forceActiveFocus()
    }

    TerminalView {
        id: view
        objectName: pane.split ? "terminalView_" + pane.index : "terminalView"
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.bottom: exited.visible ? exited.top : parent.bottom
        session: pane.session
        dark: pane.dark
        font.family: Theme.fontMono
        font.pixelSize: Theme.textBase
        padding: Theme.space2
        focus: true
        appShortcuts: pane.appShortcuts

        Accessible.role: Accessible.Terminal
        Accessible.name: pane.session !== null ? pane.session.title : qsTr("Terminal")
        Accessible.description: view.accessibleText

        onActiveFocusChanged: {
            if (view.activeFocus)
                pane.focusRequested()
        }
        onLinkActivated: (url) => pane.linkActivated(url)
        onFileActivated: (path, line, column) => pane.fileActivated(path, line, column)
        onContextMenuRequested: (x, y) => {
            view.forceActiveFocus()
            pane.menuRequested(x + view.x, y + view.y)
        }
        onFindRequested: pane.findRequested()

        // A long press on a touch screen: the menu (a right click is the view's own).
        TapHandler {
            id: longPress
            acceptedDevices: PointerDevice.TouchScreen
            onLongPressed: {
                const at = longPress.point.position
                pane.menuRequested(at.x + view.x, at.y + view.y)
            }
        }
    }

    // The scroll bar: the screen's place in the scrollback (shown once there is one), over the
    // view's right padding — never taking a column from the program when the scrollback begins.
    Item {
        id: scrollTrack
        z: 1
        objectName: "terminalScrollBar_" + pane.index
        readonly property int total: view.historySize + view.rows
        readonly property real thumbSize: scrollTrack.total > 0 ? Math.max(24, scrollTrack.height * view.rows / scrollTrack.total) : scrollTrack.height
        readonly property real thumbY: scrollTrack.total > view.rows
                                       ? (scrollTrack.height - scrollTrack.thumbSize) * (view.historySize - view.scrollOffset) / view.historySize
                                       : 0
        anchors.top: parent.top
        anchors.bottom: view.bottom
        anchors.right: parent.right
        width: view.padding
        visible: view.historySize > 0

        Rectangle {
            id: thumb
            x: 2
            y: scrollTrack.thumbY
            width: Math.max(2, scrollTrack.width - 4)
            height: scrollTrack.thumbSize
            radius: width / 2
            color: Theme.scrollbar
            opacity: dragArea.pressed || dragArea.containsMouse ? 1 : 0.6
        }
        MouseArea {
            id: dragArea
            anchors.fill: parent
            hoverEnabled: !Theme.touchUi
            preventStealing: true
            property real grab: 0
            function scrollTo(y: real): void {
                const room = scrollTrack.height - scrollTrack.thumbSize
                if (room <= 0)
                    return
                const top = Math.max(0, Math.min(room, y - dragArea.grab))
                view.scrollOffset = Math.round(view.historySize * (1 - top / room))
            }
            onPressed: (mouse) => {
                const y = mouse.y
                // On the thumb: dragged from where it was taken; beside it: centred there.
                const onThumb = y >= thumb.y && y <= thumb.y + thumb.height
                dragArea.grab = onThumb ? y - thumb.y : scrollTrack.thumbSize / 2
                dragArea.scrollTo(y)
            }
            onPositionChanged: (mouse) => {
                if (dragArea.pressed)
                    dragArea.scrollTo(mouse.y)
            }
        }
    }

    // Scrolled back: back to the newest output.
    BButton {
        objectName: "terminalJumpLatest_" + pane.index
        anchors.right: parent.right
        z: 1
        anchors.bottom: view.bottom
        anchors.margins: Theme.space3
        visible: view.scrollOffset > 0
        size: "small"
        variant: "secondary"
        iconName: "arrow-down"
        text: qsTr("Jump to latest")
        focusPolicy: Qt.NoFocus
        onClicked: {
            view.scrollToBottom()
            view.forceActiveFocus()
        }
    }

    // The focused terminal of a split tab.
    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 2
        visible: pane.split && pane.focusedPane
        color: Theme.accent
    }

    // The program ended (or could not start).
    Rectangle {
        id: exited
        objectName: "terminalExited_" + pane.index
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        visible: pane.exitText() !== ""
        implicitHeight: exitedRow.implicitHeight + 2 * Theme.space2
        height: exited.visible ? exited.implicitHeight : 0
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
                name: pane.session !== null && pane.session.hasExited && pane.session.exitCode === 0 ? "circle-check" : "circle-x"
                size: Theme.iconSizeSm
                color: Theme.fgMuted
            }
            BText {
                objectName: "terminalExitedText_" + pane.index
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                variant: "muted"
                text: pane.exitText()
                elide: Text.ElideRight
            }
            BButton {
                objectName: "terminalRestart_" + pane.index
                Layout.alignment: Qt.AlignVCenter
                text: qsTr("Restart")
                iconName: "rotate-ccw"
                size: "small"
                onClicked: pane.restart()
            }
            BIconButton {
                objectName: "terminalCloseExited_" + pane.index
                Layout.alignment: Qt.AlignVCenter
                iconName: "x"
                size: "small"
                label: qsTr("Close terminal")
                onClicked: pane.closeRequested()
            }
        }
    }
}
