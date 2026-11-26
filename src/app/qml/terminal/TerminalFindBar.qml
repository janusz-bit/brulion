pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// TerminalFindBar — the terminal panel's search in the scrollback (docs/CODE-TERMINAL.md "The
// panel"): the query, "3 of 12", older / newer, "Aa" for letter case, close. Every match of the
// shown terminal is highlighted (TerminalView.find: case-insensitive unless "Aa", found again a
// moment after new output); the newest is the current one at first, Enter goes to the one before it
// (up: older output), Shift+Enter back down; Esc closes the bar (closeRequested(): the panel gives
// the keyboard back to the terminal). Ctrl+Shift+F in the terminal opens it.
//
//   TerminalFindBar { view: pane.view; onCloseRequested: … }
//
// objectNames: terminalFindBar, terminalFindField, terminalFindStatus, terminalFindOlder,
// terminalFindNewer, terminalFindCase, terminalFindClose.
Rectangle {
    id: bar
    objectName: "terminalFindBar"

    /// The terminal searched (the panel's focused one).
    property TerminalView view: null
    property bool matchCase: false
    /// The bar is open (shown while it is; the panel hidden with it open keeps its matches).
    property bool open: false
    visible: bar.open

    signal closeRequested()

    function focusField(): void {
        field.forceActiveFocus()
        field.selectAll()
    }
    /// The terminal whose matches are shown (another one focused: its own, these go).
    property TerminalView searched: null
    function search(): void {
        if (bar.searched !== null && bar.searched !== bar.view)
            bar.searched.clearFind()
        bar.searched = bar.open ? bar.view : null
        if (bar.searched !== null)
            bar.searched.find(field.text, bar.matchCase)
    }
    /// The bar closed: no matches shown anywhere.
    function clear(): void {
        if (bar.searched !== null)
            bar.searched.clearFind()
        bar.searched = null
    }
    function older(): void {
        if (bar.view !== null)
            bar.view.findOlder()
    }
    function newer(): void {
        if (bar.view !== null)
            bar.view.findNewer()
    }
    /// The status text: "", "No results", "3 of 12".
    function status(): string {
        if (bar.view === null || bar.view.findText === "")
            return ""
        if (bar.view.findCount === 0)
            return qsTr("No results")
        //: Matches in the terminal: "3 of 12"
        return qsTr("%1 of %2").arg(bar.view.findIndex + 1).arg(bar.view.findCount)
    }

    // Another terminal shown: the query goes on there (only while the bar is open).
    onViewChanged: bar.search()
    onMatchCaseChanged: bar.search()
    onOpenChanged: {
        if (bar.open)
            bar.search()
        else
            bar.clear()
    }

    implicitHeight: row.implicitHeight + 2 * Theme.space1
    implicitWidth: row.implicitWidth + Theme.space2 + Theme.space1
    radius: Theme.radiusSm
    color: Theme.bgElevated
    border.width: 1
    border.color: Theme.border

    Accessible.role: Accessible.ToolBar
    Accessible.name: qsTr("Find in terminal")

    RowLayout {
        id: row
        anchors.fill: parent
        anchors.leftMargin: Theme.space2
        anchors.rightMargin: Theme.space1
        spacing: Theme.space1

        BSearchField {
            id: field
            objectName: "terminalFindField"
            Layout.preferredWidth: 220
            Layout.minimumWidth: 100
            size: "small"
            debounce: 100
            clearable: false
            placeholderText: qsTr("Find in terminal")
            onSearch: (query) => bar.search()
            property int lastModifiers: 0
            Keys.priority: Keys.BeforeItem
            Keys.onPressed: (event) => {
                field.lastModifiers = event.modifiers
                if (event.key === Qt.Key_Escape) {
                    bar.closeRequested()
                    event.accepted = true
                } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    // (The query as typed now, before the step: BSearchField's search() may wait.)
                    if (bar.view !== null && bar.view.findText !== field.text)
                        bar.search()
                    else if (event.modifiers & Qt.ShiftModifier)
                        bar.newer()
                    else
                        bar.older()
                    event.accepted = true
                }
            }
        }
        Text {
            objectName: "terminalFindStatus"
            Layout.alignment: Qt.AlignVCenter
            Layout.minimumWidth: 48
            textFormat: Text.PlainText
            font.family: Theme.fontUi
            font.pixelSize: Theme.textSm
            color: Theme.fgMuted
            text: bar.status()
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
        BButton {
            id: caseButton
            objectName: "terminalFindCase"
            size: "small"
            text: "Aa"
            checkable: true
            checked: bar.matchCase
            variant: bar.matchCase ? "secondary" : "ghost"
            focusPolicy: Qt.NoFocus
            Accessible.name: qsTr("Match case")
            onToggled: bar.matchCase = caseButton.checked
            BToolTip {
                visible: caseButton.hovered && !Theme.touchUi
                text: qsTr("Match case")
            }
        }
        BIconButton {
            objectName: "terminalFindOlder"
            size: "small"
            iconName: "chevron-up"
            label: qsTr("Previous match (older output)")
            shortcut: "Return"
            enabled: bar.view !== null && bar.view.findCount > 0
            focusPolicy: Qt.NoFocus
            onClicked: bar.older()
        }
        BIconButton {
            objectName: "terminalFindNewer"
            size: "small"
            iconName: "chevron-down"
            label: qsTr("Next match (newer output)")
            shortcut: "Shift+Return"
            enabled: bar.view !== null && bar.view.findCount > 0
            focusPolicy: Qt.NoFocus
            onClicked: bar.newer()
        }
        BIconButton {
            objectName: "terminalFindClose"
            size: "small"
            iconName: "x"
            label: qsTr("Close find")
            shortcut: "Esc"
            focusPolicy: Qt.NoFocus
            onClicked: bar.closeRequested()
        }
    }
}
