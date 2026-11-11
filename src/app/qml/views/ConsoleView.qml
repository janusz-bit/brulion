pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ConsoleView — route "console" (docs/UI.md "Extensions"): the Luau console. A line of Luau runs
// in an environment of its own with the notebook's API (`b`, docs/EXTENSIONS.md): an expression
// shows its value ("b.today()", "#b.pages({ tag = "projekt" })"), statements run, globals stay
// for the next line (a REPL, Emacs' ielm). Below the input come print() and b.log of every
// extension and their errors (Emacs' *Messages*), and "Run Luau block" (a ```luau block of a
// page). Up / Down go through what was typed; Copy and Clear.
//
// Contract with the shell (qml/shell/ShellRouteView.qml): `route`, optional `title`.
FocusScope {
    id: root

    property var route: ({})
    readonly property string title: qsTr("Luau console")
    readonly property ExtensionConsoleModel output: Workspace.extensions.console
    /// Up / Down in the history: -1 = the line being typed.
    property int historyIndex: -1
    property string draft

    function run(): void {
        const code = input.text
        if (Js.trim(code) === "")
            return
        root.historyIndex = -1
        input.text = ""
        Workspace.extensions.evaluate(code)
        toEnd.restart()
    }
    function recall(step: int): void {
        const next = root.historyIndex + step
        if (next < -1 || next >= root.output.inputCount)
            return
        if (root.historyIndex === -1)
            root.draft = input.text
        root.historyIndex = next
        input.text = next === -1 ? root.draft : root.output.inputAt(next)
        input.cursorPosition = input.text.length
    }

    focus: true
    Component.onCompleted: input.forceActiveFocus()

    Later {
        id: toEnd
        onTriggered: lines.positionViewAtEnd()
    }
    Connections {
        target: root.output
        function onCountChanged() {
            toEnd.restart()
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.bg
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.narrow ? Theme.space3 : Theme.space6
        anchors.rightMargin: Theme.narrow ? Theme.space3 : Theme.space6
        anchors.topMargin: Theme.narrow ? Theme.space3 : Theme.space5
        anchors.bottomMargin: Theme.space3
        spacing: Theme.space3

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space2
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                BText {
                    Layout.fillWidth: true
                    text: root.title
                    variant: "heading"
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                BText {
                    Layout.fillWidth: true
                    variant: "muted"
                    elide: Text.ElideRight
                    text: qsTr("Luau with the notebook's API; what extensions print")
                }
            }
            BIconButton {
                objectName: "consoleCopy"
                label: qsTr("Copy everything")
                iconName: "copy"
                enabled: root.output.count > 0
                onClicked: {
                    if (Logs.copyText(root.output.text()))
                        AppController.toast(qsTr("Copied"), "success")
                }
            }
            BIconButton {
                objectName: "consoleClear"
                label: qsTr("Clear")
                iconName: "eraser"
                enabled: root.output.count > 0
                onClicked: root.output.clear()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.radiusLg
            color: Theme.bgSunken
            border.color: Theme.borderSubtle

            ListView {
                id: lines
                objectName: "consoleLines"
                anchors.fill: parent
                anchors.margins: Theme.space3
                clip: true
                model: root.output
                spacing: 2
                TouchFlick.enabled: true
                boundsBehavior: Flickable.StopAtBounds
                BRowScrollBar { list: lines }
                Accessible.role: Accessible.List
                Accessible.name: root.title

                delegate: Item {
                    id: line
                    required property string lineKind
                    required property string lineText
                    required property string lineSource
                    width: ListView.view.width
                    height: lineLabel.implicitHeight + (line.lineKind === "input" ? Theme.space1 : 0)

                    Text {
                        id: lineLabel
                        y: line.lineKind === "input" ? Theme.space1 : 0
                        width: parent.width
                        textFormat: Text.PlainText
                        wrapMode: Text.WrapAnywhere
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.textSm
                        color: line.lineKind === "error" ? Theme.dangerText
                                                         : line.lineKind === "input" ? Theme.link
                                                                                     : line.lineKind === "print" || line.lineKind === "info" ? Theme.fgMuted
                                                                                                                                              : Theme.fg
                        text: line.lineKind === "input" ? "> " + Js.replaceAll(line.lineText, "\n", "\n  ")
                                                        : line.lineSource !== "" ? line.lineSource + ": " + line.lineText
                                                                                 : line.lineText
                    }
                }
            }

            BText {
                anchors.centerIn: parent
                width: Math.min(parent.width - 2 * Theme.space4, 460)
                visible: root.output.count === 0
                variant: "muted"
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                text: qsTr("Type Luau below and press Enter, for example:\nb.today()\n#b.pages({})\nb.blocks({ tag = \"projekt\" })\nprint() of your extensions shows up here too.")
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space2
            BTextField {
                id: input
                objectName: "consoleInput"
                Layout.fillWidth: true
                font.family: Theme.fontMono
                placeholderText: qsTr("Luau, e.g. b.today()")
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                onAccepted: root.run()
                Keys.onUpPressed: (event) => {
                    root.recall(1)
                    event.accepted = true
                }
                Keys.onDownPressed: (event) => {
                    root.recall(-1)
                    event.accepted = true
                }
            }
            BButton {
                objectName: "consoleRun"
                text: Theme.narrow ? "" : qsTr("Run")
                iconName: "play"
                variant: "primary"
                Accessible.name: qsTr("Run")
                onClicked: root.run()
            }
        }
    }
}
