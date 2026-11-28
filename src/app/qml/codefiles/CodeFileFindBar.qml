pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// CodeFileFindBar — find within the file shown in the file viewer (docs/CODE-FILES.md "Find in the
// file"; CodeFilesPanel, Ctrl+F there): the query, "3 of 12", previous / next, "Aa" (match case),
// "ab" (whole words), ".*" (a regular expression), close. Every match of `document` is bold in the
// match colour (CodeFileDocument.find); Enter goes to the next one, Shift+Enter to the one before,
// each shown and marked (lineRequested(line)); Esc closes the bar (closeRequested()).
//
// objectNames: codeFileFindBar, codeFileFindField, codeFileFindStatus, codeFileFindPrevious,
// codeFileFindNext, codeFileFindCase, codeFileFindWord, codeFileFindRegex, codeFileFindClose.
Rectangle {
    id: bar
    objectName: "codeFileFindBar"

    property CodeFileDocument document: null
    property bool open: false
    /// The match shown (-1 none yet).
    property int current: -1
    visible: bar.open

    signal lineRequested(int line)
    signal closeRequested()

    function focusField(): void {
        field.forceActiveFocus()
        field.selectAll()
    }
    function search(): void {
        bar.current = -1
        if (bar.document !== null)
            bar.document.find(bar.open ? field.text : "", caseButton.checked, wordButton.checked, regexButton.checked)
    }
    /// The next match (`delta` 1) or the one before (-1), shown.
    function step(delta: int): void {
        if (bar.document === null)
            return
        bar.current = bar.document.nextMatch(bar.current, delta)
        if (bar.current >= 0)
            bar.lineRequested(bar.document.matchLine(bar.current))
    }
    /// "", "No results", "3 of 12", or why the expression is none.
    function status(): string {
        if (bar.document === null || field.text === "")
            return ""
        if (bar.document.findError !== "")
            return bar.document.findError
        if (bar.document.matchCount === 0)
            return qsTr("No results")
        if (bar.current < 0)
            return qsTr("%n result(s)", "", bar.document.matchCount)
        //: Matches in the file: "3 of 12"
        return qsTr("%1 of %2").arg(bar.current + 1).arg(bar.document.matchCount)
    }

    onOpenChanged: bar.search()

    implicitHeight: row.implicitHeight + 2 * Theme.space1
    color: Theme.bgElevated
    Accessible.role: Accessible.ToolBar
    Accessible.name: qsTr("Find in file")

    RowLayout {
        id: row
        anchors.fill: parent
        anchors.leftMargin: Theme.space2
        anchors.rightMargin: Theme.space1
        spacing: Theme.space1

        BSearchField {
            id: field
            objectName: "codeFileFindField"
            Layout.fillWidth: true
            Layout.minimumWidth: 100
            size: "small"
            debounce: 150
            clearable: false
            placeholderText: qsTr("Find in file")
            onSearch: (query) => bar.search()
            Keys.priority: Keys.BeforeItem
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Escape) {
                    bar.closeRequested()
                    event.accepted = true
                } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    bar.step((event.modifiers & Qt.ShiftModifier) ? -1 : 1)
                    event.accepted = true
                }
            }
        }
        BText {
            objectName: "codeFileFindStatus"
            Layout.maximumWidth: 200
            text: bar.status()
            variant: "small"
            color: bar.document !== null && bar.document.findError !== "" ? Theme.dangerText : Theme.fgMuted
            elide: Text.ElideRight
        }
        BIconButton {
            objectName: "codeFileFindPrevious"
            size: "small"
            iconName: "chevron-up"
            label: qsTr("Previous match")
            enabled: bar.document !== null && bar.document.matchCount > 0
            onClicked: bar.step(-1)
        }
        BIconButton {
            objectName: "codeFileFindNext"
            size: "small"
            iconName: "chevron-down"
            label: qsTr("Next match")
            enabled: bar.document !== null && bar.document.matchCount > 0
            onClicked: bar.step(1)
        }
        BButton {
            id: caseButton
            focusPolicy: Qt.NoFocus
            objectName: "codeFileFindCase"
            size: "small"
            variant: caseButton.checked ? "secondary" : "ghost"
            text: "Aa"
            checkable: true
            Accessible.name: qsTr("Match case")
            onToggled: bar.search()
        }
        BButton {
            id: wordButton
            focusPolicy: Qt.NoFocus
            objectName: "codeFileFindWord"
            size: "small"
            variant: wordButton.checked ? "secondary" : "ghost"
            text: "ab"
            checkable: true
            Accessible.name: qsTr("Whole words")
            onToggled: bar.search()
        }
        BButton {
            id: regexButton
            focusPolicy: Qt.NoFocus
            objectName: "codeFileFindRegex"
            size: "small"
            variant: regexButton.checked ? "secondary" : "ghost"
            text: ".*"
            checkable: true
            Accessible.name: qsTr("Regular expression")
            onToggled: bar.search()
        }
        BIconButton {
            objectName: "codeFileFindClose"
            size: "small"
            iconName: "x"
            label: qsTr("Close")
            onClicked: bar.closeRequested()
        }
    }
}
