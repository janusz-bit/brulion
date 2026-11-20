pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ChatActivity — what an AI agent's turn does, under its answer as it is written (docs/UI.md "AI
// agents (ACP)"; ChatView): its plan, a step a line with its state, and its tool calls — the newest
// few (a tap on "N earlier" shows them all), each its title with an icon of its state, and a
// command's output (a terminal) or a result under it: its last lines, plain monospace text (it
// quotes files and programs). The turn's ChatTurn.tools / ChatTurn.plan.
Rectangle {
    id: card

    property list<var> tools
    property list<var> plan
    /// Every call is shown (else the newest `maxShown`).
    property bool expanded: false
    readonly property int maxShown: 4
    readonly property int hiddenCount: Math.max(0, card.tools.length - card.maxShown)

    visible: card.tools.length > 0 || card.plan.length > 0
    implicitHeight: column.implicitHeight + 2 * Theme.space2
    radius: Theme.radiusLg
    color: Theme.bgSunken

    function stateIcon(status: string): string {
        return status === "completed" ? "check" : status === "failed" ? "circle-x" : status === "in_progress" ? "terminal" : "circle"
    }

    ColumnLayout {
        id: column
        x: Theme.space3
        y: Theme.space2
        width: parent.width - 2 * Theme.space3
        spacing: Theme.space1

        // The plan.
        Repeater {
            model: card.plan
            delegate: RowLayout {
                id: step
                required property var modelData
                readonly property string status: AppController.routeText(step.modelData, "status")
                objectName: "chatPlanEntry"
                Layout.fillWidth: true
                spacing: Theme.space2
                BIcon {
                    name: step.status === "completed" ? "check" : step.status === "in_progress" ? "list-checks" : "square"
                    size: 14
                    color: step.status === "completed" ? Theme.success : Theme.fgMuted
                }
                BText {
                    Layout.fillWidth: true
                    text: AppController.routeText(step.modelData, "content")
                    textFormat: Text.PlainText
                    variant: step.status === "completed" ? "faint" : "body"
                    font.pixelSize: Theme.textSm
                    font.weight: step.status === "in_progress" ? Font.DemiBold : Font.Normal
                    wrapMode: Text.Wrap
                }
            }
        }

        BText {
            objectName: "chatToolsEarlier"
            visible: card.hiddenCount > 0 && !card.expanded
            text: qsTr("%n earlier step(s)", "", card.hiddenCount)
            variant: "faint"
            font.pixelSize: Theme.textSm
            TapHandler {
                onTapped: card.expanded = true
            }
        }

        // The tool calls, the newest last.
        Repeater {
            model: card.tools
            delegate: ColumnLayout {
                id: call
                required property var modelData
                required property int index
                readonly property string status: AppController.routeText(call.modelData, "status")
                readonly property string output: AppController.routeText(call.modelData, "output")
                readonly property bool hasDiff: AppController.routeText(call.modelData, "diffPath") !== ""
                /// Its change is shown (a tap on "Show the change").
                property bool diffOpen: false
                objectName: "chatToolCall"
                Layout.fillWidth: true
                visible: card.expanded || call.index >= card.tools.length - card.maxShown
                spacing: 2
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.space2
                    BIcon {
                        name: card.stateIcon(call.status)
                        size: 14
                        color: call.status === "completed" ? Theme.success : call.status === "failed" ? Theme.danger : Theme.fgMuted
                    }
                    BText {
                        Layout.fillWidth: true
                        text: AppController.routeText(call.modelData, "title")
                        textFormat: Text.PlainText
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.textSm
                        elide: Text.ElideRight
                        maximumLineCount: 2
                        wrapMode: Text.Wrap
                    }
                }
                BText {
                    objectName: "chatToolDiffToggle"
                    Layout.leftMargin: 14 + Theme.space2
                    visible: call.hasDiff
                    text: call.diffOpen ? qsTr("Hide the change") : qsTr("Show the change")
                    color: Theme.accentText
                    font.pixelSize: Theme.textSm
                    TapHandler {
                        onTapped: call.diffOpen = !call.diffOpen
                    }
                }
                BDiffView {
                    id: toolDiff
                    objectName: "chatToolDiff"
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(220, toolDiff.rows.length * 24 + 8)
                    visible: call.hasDiff && call.diffOpen
                    mode: "unified"
                    showToolbar: false
                    oldText: call.diffOpen ? AppController.routeText(call.modelData, "diffOld") : ""
                    newText: call.diffOpen ? AppController.routeText(call.modelData, "diffNew") : ""
                    oldLabel: AppController.routeText(call.modelData, "diffPath")
                    newLabel: AppController.routeText(call.modelData, "diffPath")
                }
                BText {
                    objectName: "chatToolOutput"
                    Layout.fillWidth: true
                    Layout.leftMargin: 14 + Theme.space2
                    // The last lines only: the chat is no terminal.
                    visible: call.output !== "" && (call.status === "in_progress" || call.status === "pending"
                                                    || call.index === card.tools.length - 1)
                    text: call.output
                    textFormat: Text.PlainText
                    variant: "muted"
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.textSm
                    wrapMode: Text.WrapAnywhere
                    maximumLineCount: 6
                    elide: Text.ElideLeft
                }
            }
        }
    }
}
