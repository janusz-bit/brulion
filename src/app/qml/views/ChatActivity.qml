pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ChatActivity — what an AI agent's turn does, under its answer as it is written (docs/UI.md "AI
// agents (ACP)"; ChatView): its plan, a step a line with its state, and its tool calls — a summary
// line in words ("Read 3 files · ran 2 commands", WorkspaceChats.toolSummary) that opens and closes
// their list: the newest few (a tap on "N earlier" shows them all), each its title with an icon of
// its state, and a command's output (a terminal) or a result under it: its last lines, plain
// monospace text (it quotes files and programs). Closed, the calls that run and those that failed
// stay. "Tool calls in chat" (AppSettings.chatToolCalls, `shown`): "all" — the list open —,
// "hideFinished" — closed —, "hideAll" — no summary, only failed calls; the plan always shows. The
// turn's ChatTurn.tools / ChatTurn.plan. Once the turn answered (`finished`, its work log above the
// answer — WorkspaceChats.workLog): one folded line "Worked for 25 s · Ran 2 commands" that opens
// the plan and the calls as they ended.
Rectangle {
    id: card

    property list<var> tools
    property list<var> plan
    /// A turn that ended (its work log under its answer, WorkspaceChats.workLog): folded to one line
    /// "Worked for 25 s · Ran 2 commands" (as T3 Code's) that opens its plan and calls; `seconds` how
    /// long it took (-1 not known).
    property bool finished: false
    property int seconds: -1
    /// A thread's folder (docs/CODE-FILES.md): the files a call works on are listed under it, a tap
    /// opens one (fileActivated: its path relative to the folder).
    property string filesFolder
    signal fileActivated(string path)
    /// AppSettings.chatToolCalls: "all" | "hideFinished" | "hideAll".
    property string shown: AppSettings.chatToolCalls
    /// The list of calls is open (else only the running and the failed ones); a tap on the summary.
    property bool listOpen: card.shown === "all" && !card.finished
    /// Every call is shown (else the newest `maxShown`).
    property bool expanded: false
    readonly property int maxShown: 4
    readonly property int hiddenCount: Math.max(0, card.tools.length - card.maxShown)
    readonly property int failedCount: card.countFailed()
    /// The calls in words ("" none); a finished turn's with how long it worked.
    readonly property string calls: card.tools.length > 0 ? Workspace.chats.toolSummary(card.tools) : ""
    readonly property string summary: !card.finished ? card.calls
                                      : card.seconds >= 0 ? (card.calls !== "" ? qsTr("Worked for %1 · %2").arg(card.durationText(card.seconds)).arg(card.calls)
                                                                               : qsTr("Worked for %1").arg(card.durationText(card.seconds)))
                                      : card.calls
    readonly property bool summaryShown: (card.tools.length > 0 || (card.finished && card.plan.length > 0)) && card.shown !== "hideAll"
    /// The plan shows: always while the turn runs, once it ended only with its list open.
    readonly property bool planShown: card.plan.length > 0 && (!card.finished || card.listOpen)

    visible: card.planShown || card.summaryShown || card.failedCount > 0

    /// "25 s", "4 min 5 s", "1 h 2 min".
    function durationText(total: int): string {
        if (total < 60)
            return qsTr("%1 s", "a duration in seconds").arg(total)
        if (total < 3600)
            return total % 60 === 0 ? qsTr("%1 min", "a duration in minutes").arg(Math.floor(total / 60))
                                    : qsTr("%1 min %2 s", "a duration").arg(Math.floor(total / 60)).arg(total % 60)
        return qsTr("%1 h %2 min", "a duration").arg(Math.floor(total / 3600)).arg(Math.floor((total % 3600) / 60))
    }
    // A new turn: as the setting says again.
    onToolsChanged: if (card.tools.length === 0) {
        card.listOpen = card.shown === "all" && !card.finished
        card.expanded = false
    }
    onShownChanged: card.listOpen = card.shown === "all" && !card.finished
    readonly property list<var> noSteps: []
    implicitHeight: column.implicitHeight + 2 * Theme.space2
    radius: Theme.radiusLg
    color: Theme.bgSunken

    readonly property list<string> noFiles: []
    function stateIcon(status: string): string {
        return status === "completed" ? "check" : status === "failed" ? "circle-x" : status === "in_progress" ? "terminal" : "circle"
    }
    function countFailed(): int {
        let n = 0
        for (let i = 0; i < card.tools.length; ++i) {
            if (AppController.routeText(card.tools[i], "status") === "failed")
                ++n
        }
        return n
    }
    /// The call `index` (its `status`) is shown.
    function callShown(status: string, index: int): bool {
        if (status === "failed")
            return true
        if (card.shown === "hideAll")
            return false
        if (!card.listOpen)
            return !card.finished && (status === "in_progress" || status === "pending")
        return card.expanded || index >= card.tools.length - card.maxShown
    }
    /// `text` (ChatTurn.status) is the title of one of the calls: with "Hide all", ChatView says
    /// only that the agent works.
    function hidesStatus(text: string): bool {
        if (card.shown !== "hideAll" || text === "")
            return false
        for (let i = 0; i < card.tools.length; ++i) {
            if (AppController.routeText(card.tools[i], "title") === text)
                return true
        }
        return false
    }

    ColumnLayout {
        id: column
        x: Theme.space3
        y: Theme.space2
        width: parent.width - 2 * Theme.space3
        spacing: Theme.space1

        // The plan.
        Repeater {
            model: card.planShown ? card.plan : card.noSteps
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

        // The calls in words; a tap opens or closes their list.
        Item {
            id: summaryRow
            objectName: "chatToolsSummary"
            Layout.fillWidth: true
            visible: card.summaryShown
            implicitHeight: summaryLine.implicitHeight
            Accessible.role: Accessible.Button
            Accessible.name: card.summary
            Accessible.description: card.listOpen ? qsTr("Hide the steps") : qsTr("Show the steps")
            RowLayout {
                id: summaryLine
                width: parent.width
                spacing: Theme.space2
                BIcon {
                    name: card.listOpen ? "chevron-down" : "chevron-right"
                    size: 14
                    color: Theme.fgMuted
                }
                BText {
                    objectName: "chatToolsSummaryText"
                    Layout.fillWidth: true
                    text: card.summary
                    textFormat: Text.PlainText
                    variant: "muted"
                    font.pixelSize: Theme.textSm
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
            }
            TapHandler {
                onTapped: card.listOpen = !card.listOpen
            }
            HoverHandler {
                enabled: !Theme.touchUi
                cursorShape: Qt.PointingHandCursor
            }
        }

        BText {
            objectName: "chatToolsEarlier"
            visible: card.listOpen && card.shown !== "hideAll" && card.hiddenCount > 0 && !card.expanded
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
                /// The files it works on inside the thread's folder, relative to it.
                readonly property list<string> files: card.filesFolder !== ""
                                                      ? CodeFiles.relativePaths(card.filesFolder, AppController.routeStrings(call.modelData, "paths"))
                                                      : card.noFiles
                /// Its change is shown (a tap on "Show the change").
                property bool diffOpen: false
                objectName: "chatToolCall"
                Layout.fillWidth: true
                visible: card.callShown(call.status, call.index)
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
                // (A thread's files the call works on: a tap opens one in the file viewer.)
                Flow {
                    Layout.fillWidth: true
                    Layout.leftMargin: 14 + Theme.space2
                    visible: card.filesFolder !== "" && call.files.length > 0
                    spacing: Theme.space2
                    Repeater {
                        model: card.filesFolder !== "" ? call.files : card.noFiles
                        delegate: BText {
                            id: callFile
                            required property string modelData
                            objectName: "chatToolFile"
                            text: callFile.modelData
                            textFormat: Text.PlainText
                            color: Theme.link
                            font.family: Theme.fontMono
                            font.pixelSize: Theme.textXs
                            Accessible.role: Accessible.Link
                            TapHandler {
                                onTapped: card.fileActivated(callFile.modelData)
                            }
                            HoverHandler {
                                cursorShape: Qt.PointingHandCursor
                            }
                        }
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
