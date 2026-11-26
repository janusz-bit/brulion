pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// CodeSearchPanel — the "Search" tab of Code mode's right panel (docs/CODE-FILES.md "Project
// search"; T3 Code's mod+shift+f, here Ctrl+Shift+F): text searched for in the project's files
// (CodeSearch: ripgrep, else git grep, else Brulion's own), the results grouped by file — a file's
// row with its number of matches, then each matching line with its number and the match marked —,
// as you type (a moment after the last key; Enter at once). Toggles: match case (Aa), whole words
// (ab), regular expression (.*). The status line says how many, where they come from, that the
// search stopped at 2,000, or why nothing was found; Stop while it runs. ↓ from the field and
// ↑/↓ in the list move between the matches; Enter or a click opens the file in the viewer at the
// line (openFile).
Item {
    id: panel

    property string folder
    signal openFile(string path, int line)

    property int currentRow: -1

    /// The field focused, `text` typed in it (the selection of a viewer, say).
    function focusField(text: string): void {
        if (text !== "") {
            field.text = text
            search.start(text)
        }
        field.forceActiveFocus()
        field.selectAll()
    }
    function run(): void {
        typing.stop()
        search.start("" + field.text)
    }
    function activate(row: int): void {
        const path = search.pathAt(row)
        if (path === "")
            return
        panel.currentRow = row
        const line = search.lineAt(row)
        panel.openFile(path, line > 0 ? line : search.lineAt(search.nextMatch(row, 1)))
    }
    function moveTo(row: int): void {
        if (row < 0)
            return
        panel.currentRow = row
        results.positionViewAtIndex(row, ListView.Contain)
    }

    onFolderChanged: panel.currentRow = -1

    CodeSearch {
        id: search
        objectName: "codeSearch"
        folder: panel.folder
        markColor: Theme.link
        onOptionsChanged: if (search.pattern !== "") panel.run()
    }
    Timer {
        id: typing
        interval: 300
        onTriggered: panel.run()
    }

    // A toggle of the search: a short label in the mono font, tinted when on.
    component OptionToggle: T.AbstractButton {
        id: toggle
        required property string label
        required property string tip
        checkable: true
        focusPolicy: Qt.NoFocus
        implicitWidth: Math.max(28, toggleText.implicitWidth + 12)
        implicitHeight: 28
        hoverEnabled: !Theme.touchUi
        Accessible.name: toggle.tip
        BToolTip {
            visible: toggle.hovered
            text: toggle.tip
        }
        background: Rectangle {
            radius: Theme.radiusSm
            color: toggle.checked ? Theme.bgSelected : toggle.hovered ? Theme.bgHover : "transparent"
            border.width: toggle.checked ? 1 : 0
            border.color: Theme.link
        }
        contentItem: Text {
            id: toggleText
            text: toggle.label
            textFormat: Text.PlainText
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font.family: Theme.fontMono
            font.pixelSize: Theme.textSm
            font.weight: Theme.weightSemiBold
            color: toggle.checked ? Theme.link : Theme.fgMuted
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.space2
            Layout.rightMargin: Theme.space2
            Layout.topMargin: Theme.space2
            spacing: Theme.space1
            BTextField {
                id: field
                objectName: "codeSearchField"
                Layout.fillWidth: true
                size: "small"
                leadingIcon: "search"
                clearable: true
                placeholderText: qsTr("Search in the project")
                inputMethodHints: Qt.ImhNoPredictiveText
                onTextChanged: {
                    if (field.text === "") {
                        typing.stop()
                        search.clear()
                    } else {
                        typing.restart()
                    }
                }
                onAccepted: panel.run()
                Keys.onDownPressed: {
                    panel.moveTo(search.nextMatch(-1, 1))
                    results.forceActiveFocus()
                }
            }
            OptionToggle {
                objectName: "codeSearchCase"
                label: "Aa"
                tip: qsTr("Match case")
                checked: search.caseSensitive
                onToggled: search.caseSensitive = checked
            }
            OptionToggle {
                objectName: "codeSearchWord"
                label: "ab"
                tip: qsTr("Whole words")
                checked: search.wholeWord
                onToggled: search.wholeWord = checked
            }
            OptionToggle {
                objectName: "codeSearchRegex"
                label: ".*"
                tip: qsTr("Regular expression")
                checked: search.regex
                onToggled: search.regex = checked
            }
        }

        // ---- How many, from where; Stop --------------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.space3
            Layout.rightMargin: Theme.space2
            Layout.topMargin: Theme.space1
            Layout.bottomMargin: Theme.space1
            visible: search.running || search.done
            spacing: Theme.space2
            BSpinner {
                visible: search.running
                size: Theme.iconSizeSm
            }
            BText {
                objectName: "codeSearchStatus"
                Layout.fillWidth: true
                text: search.error !== "" ? search.error
                    : search.running ? qsTr("Searching…")
                    : search.matchCount === 0 ? qsTr("Nothing found")
                    : (search.truncated ? qsTr("The first %n result(s)", "", search.matchCount) : qsTr("%n result(s)", "", search.matchCount))
                      + " · " + qsTr("%n file(s)", "", search.fileCount)
                variant: search.error !== "" ? "body" : "faint"
                color: search.error !== "" ? Theme.dangerText : Theme.fgFaint
                font.pixelSize: Theme.textSm
                elide: Text.ElideRight
            }
            BText {
                objectName: "codeSearchEngine"
                visible: search.done && search.engine !== ""
                text: search.engine === "rg" ? "ripgrep" : search.engine === "git" ? "git grep" : qsTr("Brulion")
                variant: "faint"
                font.pixelSize: Theme.textXs
            }
            BButton {
                objectName: "codeSearchStop"
                visible: search.running
                size: "small"
                variant: "ghost"
                text: qsTr("Stop")
                onClicked: search.cancel()
            }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.borderSubtle
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: results
                objectName: "codeSearchResults"
                anchors.fill: parent
                clip: true
                model: search
                boundsBehavior: Flickable.StopAtBounds
                reuseItems: true
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Search results")
                T.ScrollBar.vertical: BScrollBar {}
                Keys.onUpPressed: {
                    const row = search.nextMatch(panel.currentRow, -1)
                    if (row < 0)
                        field.forceActiveFocus()
                    else
                        panel.moveTo(row)
                }
                Keys.onDownPressed: panel.moveTo(search.nextMatch(panel.currentRow, 1))
                Keys.onReturnPressed: panel.activate(panel.currentRow)
                Keys.onEnterPressed: panel.activate(panel.currentRow)

                delegate: T.ItemDelegate {
                    id: result
                    required property int index
                    required property string kind
                    required property string path
                    required property int line
                    required property string html
                    required property int matches
                    required property string name
                    required property string folder
                    readonly property bool isFile: result.kind === "file"
                    objectName: result.isFile ? "codeSearchFile" : "codeSearchMatch"
                    width: ListView.view.width
                    height: result.isFile ? Theme.listItemHeight : Math.max(Theme.touchUi ? 36 : 24, matchText.implicitHeight + 8)
                    focusPolicy: Qt.NoFocus
                    hoverEnabled: !Theme.touchUi
                    Accessible.name: result.isFile ? result.path : qsTr("Line %1").arg(result.line)
                    onClicked: panel.activate(result.index)
                    background: Rectangle {
                        color: result.index === panel.currentRow ? Theme.bgSelected : result.hovered ? Theme.bgHover : "transparent"
                    }
                    contentItem: Item {
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.space2
                            anchors.rightMargin: Theme.space2
                            visible: result.isFile
                            spacing: Theme.space2
                            BIcon {
                                name: CodeFiles.iconOf(result.path, false)
                                size: Theme.iconSizeSm
                                color: Theme.fgMuted
                            }
                            BText {
                                text: result.name
                                variant: "label"
                                font.pixelSize: Theme.textSm
                                elide: Text.ElideRight
                                Layout.maximumWidth: parent.width * 0.6
                            }
                            BText {
                                Layout.fillWidth: true
                                text: result.folder
                                variant: "faint"
                                font.pixelSize: Theme.textXs
                                elide: Text.ElideMiddle
                            }
                            BBadge {
                                count: result.matches
                            }
                        }
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.space6
                            anchors.rightMargin: Theme.space2
                            visible: !result.isFile
                            spacing: Theme.space2
                            Text {
                                Layout.alignment: Qt.AlignVCenter
                                Layout.preferredWidth: lineMetrics.advanceWidth("00000")
                                horizontalAlignment: Text.AlignRight
                                text: "" + result.line
                                textFormat: Text.PlainText
                                color: Theme.fgFaint
                                font.family: Theme.fontMono
                                font.pixelSize: Theme.textXs
                            }
                            Text {
                                id: matchText
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignVCenter
                                textFormat: Text.StyledText
                                text: result.html
                                color: Theme.fg
                                font.family: Theme.fontMono
                                font.pixelSize: Theme.textSm
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }
            FontMetrics {
                id: lineMetrics
                font.family: Theme.fontMono
                font.pixelSize: Theme.textXs
            }
            BEmptyState {
                objectName: "codeSearchEmpty"
                anchors.centerIn: parent
                width: Math.min(parent.width - 2 * Theme.space4, maximumWidth)
                visible: !search.running && results.count === 0
                compact: true
                iconName: "search"
                title: search.done ? qsTr("Nothing found") : qsTr("Search the project")
                description: search.done ? (search.error !== "" ? "" : qsTr("No file of the project has “%1”.").arg(search.pattern))
                                         : qsTr("Text in every file of the project, ignored files left out. Ctrl+Shift+F comes here.")
            }
        }
    }
}
