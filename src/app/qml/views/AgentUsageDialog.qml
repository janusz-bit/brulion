pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// AgentUsageDialog — what an AI agent of this computer that is Claude Code or Codex used, read from
// its own session logs (docs/UI.md "AI agents (ACP)", "Its usage"; Workspace.chats.usage,
// shell/agentusage.h): the period (7 / 30 / 90 days), the days as bars of tokens or of their cost
// (today's in the accent colour; a day's use on hover and for screen readers), this notebook only
// or all projects, the totals, a row per model (its cost, its tokens by kind, its requests), when
// the logs were read with Refresh, and the sentence that the cost is the API's price of the same
// tokens — a subscription pays nothing more. Opened from the agent's row in Settings → Extensions →
// AI agents (AgentUsageButton.qml).
BDialog {
    id: dialog

    /// The agent ("acp:<id>") and its name.
    property string partner
    property string agentName
    /// The period in days, which projects, what the bars measure.
    property int days: 30
    property bool notebookOnly: false
    property bool showCost: false

    readonly property AgentUsage usage: Workspace.chats.usage
    readonly property var info: dialog.usage.details(dialog.partner, dialog.days, dialog.notebookOnly, dialog.usage.serial)
    readonly property list<var> bars: AppController.routeList(dialog.info, "bars")
    readonly property list<var> models: AppController.routeList(dialog.info, "models")
    readonly property real maxValue: UiModels.numberField(dialog.info, dialog.showCost ? "maxCost" : "maxTokens", 0)
    readonly property bool busy: AppController.routeFlag(dialog.info, "busy")
    readonly property bool read: AppController.routeFlag(dialog.info, "read")
    readonly property bool empty: AppController.routeFlag(dialog.info, "empty")
    readonly property string failure: AppController.routeText(dialog.info, "failure")
    readonly property list<var> periods: [{ value: "7", text: qsTr("7 days") }, { value: "30", text: qsTr("30 days") }, { value: "90", text: qsTr("90 days") }]
    readonly property list<var> measures: [{ value: "tokens", text: qsTr("Tokens") }, { value: "cost", text: qsTr("Cost") }]
    readonly property list<var> scopes: [{ value: "all", text: qsTr("All projects") }, { value: "notebook", text: qsTr("This notebook only") }]
    /// The room under the bars for their dates.
    readonly property int markHeight: Theme.textXs + Theme.space2

    /// Opens it for `agent`, named `name`; its logs are read when the last reading is older than
    /// five minutes.
    function openFor(agent: string, name: string): void {
        dialog.partner = agent
        dialog.agentName = name
        dialog.usage.show(agent)
        dialog.open()
    }
    function periodOf(value: string): int {
        return value === "7" ? 7 : value === "90" ? 90 : 30
    }

    objectName: "agentUsageDialog"
    title: qsTr("Usage of %1").arg(dialog.agentName)
    subtitle: AppController.routeText(dialog.info, "where")
    iconName: "chart-column"
    preferredWidth: Theme.dialogWidthLg
    showReject: false
    acceptText: qsTr("Close")
    footerLeading: [
        BButton {
            objectName: "agentUsageRefresh"
            size: "small"
            variant: "ghost"
            iconName: "refresh-cw"
            busy: dialog.busy
            enabled: !dialog.busy
            text: qsTr("Refresh")
            onClicked: dialog.usage.refresh(dialog.partner)
        }
    ]

    Flow {
        Layout.fillWidth: true
        spacing: Theme.space2
        BSegmented {
            objectName: "agentUsagePeriod"
            size: "small"
            accessibleName: qsTr("Period")
            model: dialog.periods
            currentValue: "" + dialog.days
            onActivated: (value) => dialog.days = dialog.periodOf("" + value)
        }
        BSegmented {
            objectName: "agentUsageMeasure"
            size: "small"
            accessibleName: qsTr("Bars show")
            model: dialog.measures
            currentValue: dialog.showCost ? "cost" : "tokens"
            onActivated: (value) => dialog.showCost = value === "cost"
        }
        BSegmented {
            objectName: "agentUsageScope"
            size: "small"
            accessibleName: qsTr("Projects")
            model: dialog.scopes
            currentValue: dialog.notebookOnly ? "notebook" : "all"
            onActivated: (value) => dialog.notebookOnly = value === "notebook"
        }
    }

    BText {
        objectName: "agentUsageTotal"
        Layout.fillWidth: true
        visible: dialog.read && dialog.failure === ""
        variant: "heading"
        text: AppController.routeText(dialog.info, "total")
        wrapMode: Text.Wrap
    }
    BText {
        objectName: "agentUsageChecked"
        Layout.fillWidth: true
        visible: text !== ""
        variant: "caption"
        text: AppController.routeText(dialog.info, "checked")
    }
    BText {
        objectName: "agentUsageFailure"
        Layout.fillWidth: true
        visible: dialog.failure !== "" || (!dialog.read && dialog.busy)
        variant: "muted"
        text: dialog.failure !== "" ? dialog.failure : qsTr("Reading the session logs…")
        wrapMode: Text.Wrap
    }

    // The days: a bar each, oldest on the left.
    Item {
        id: chart
        objectName: "agentUsageChart"
        Layout.fillWidth: true
        Layout.preferredHeight: 150
        visible: dialog.read && dialog.failure === ""
        readonly property real gap: dialog.bars.length > 40 ? 1 : 3
        readonly property real barWidth: Math.max(1, (chart.width - chart.gap * Math.max(0, dialog.bars.length - 1))
                                                      / Math.max(1, dialog.bars.length))
        readonly property real plotHeight: chart.height - dialog.markHeight
        Accessible.role: Accessible.Chart
        Accessible.name: dialog.showCost ? qsTr("Cost per day") : qsTr("Tokens per day")

        Rectangle {
            x: 0
            y: chart.plotHeight
            width: chart.width
            height: 1
            color: Theme.border
        }
        BText {
            objectName: "agentUsageEmpty"
            anchors.centerIn: parent
            visible: dialog.empty
            variant: "muted"
            text: AppController.routeText(dialog.info, "emptyText")
        }
        Repeater {
            model: dialog.bars
            delegate: Item {
                id: bar
                required property var modelData
                required property int index
                readonly property real value: UiModels.numberField(bar.modelData, dialog.showCost ? "cost" : "tokens", 0)
                readonly property string tip: AppController.routeText(bar.modelData, "tip")
                readonly property string mark: AppController.routeText(bar.modelData, "mark")
                readonly property bool isToday: AppController.routeFlag(bar.modelData, "today")
                objectName: "agentUsageBar"
                x: bar.index * (chart.barWidth + chart.gap)
                width: chart.barWidth
                height: chart.height
                Accessible.role: Accessible.StaticText
                Accessible.name: bar.tip

                Rectangle {
                    objectName: "agentUsageBarFill"
                    width: parent.width
                    height: bar.value > 0 && dialog.maxValue > 0 ? Math.max(2, chart.plotHeight * bar.value / dialog.maxValue) : 0
                    y: chart.plotHeight - height
                    radius: Math.min(2, width / 2)
                    color: hover.containsMouse ? Theme.accentHover : bar.isToday ? Theme.accent : Theme.accent.alpha(0.55)
                }
                BText {
                    visible: bar.mark !== ""
                    y: chart.plotHeight + 2
                    x: Math.min(0, chart.width - bar.x - implicitWidth)
                    variant: "caption"
                    font.pixelSize: Theme.textXs
                    text: bar.mark
                }
                MouseArea {
                    id: hover
                    anchors.fill: parent
                    hoverEnabled: !Theme.touchUi
                    acceptedButtons: Qt.NoButton
                }
                Loader {
                    active: hover.containsMouse
                    sourceComponent: BToolTip {
                        parent: bar
                        text: bar.tip
                        visible: hover.containsMouse
                    }
                }
            }
        }
    }

    // Each model: its cost, its tokens by kind, its requests.
    BText {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space2
        visible: dialog.models.length > 0
        variant: "title"
        text: qsTr("By model")
    }
    Repeater {
        model: dialog.models
        delegate: ColumnLayout {
            id: modelRow
            required property var modelData
            objectName: "agentUsageModel"
            Layout.fillWidth: true
            spacing: 0
            Accessible.role: Accessible.StaticText
            Accessible.name: qsTr("%1: %2, %3, %4").arg(AppController.routeText(modelRow.modelData, "name"))
                                                   .arg(AppController.routeText(modelRow.modelData, "cost"))
                                                   .arg(AppController.routeText(modelRow.modelData, "kinds"))
                                                   .arg(AppController.routeText(modelRow.modelData, "requests"))
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.space2
                BText {
                    Layout.fillWidth: true
                    text: AppController.routeText(modelRow.modelData, "name")
                    font.family: Theme.fontMono
                    elide: Text.ElideRight
                }
                BText {
                    objectName: "agentUsageModelCost"
                    text: AppController.routeText(modelRow.modelData, "cost")
                    font.weight: Theme.weightMedium
                }
            }
            BText {
                Layout.fillWidth: true
                variant: "caption"
                text: qsTr("%1 · %2").arg(AppController.routeText(modelRow.modelData, "kinds"))
                                     .arg(AppController.routeText(modelRow.modelData, "requests"))
                wrapMode: Text.Wrap
            }
        }
    }

    BText {
        objectName: "agentUsageUnpriced"
        Layout.fillWidth: true
        visible: text !== ""
        variant: "caption"
        color: Theme.warningText
        text: AppController.routeText(dialog.info, "unpriced")
        wrapMode: Text.Wrap
    }
    BText {
        objectName: "agentUsageNote"
        Layout.fillWidth: true
        Layout.topMargin: Theme.space2
        variant: "caption"
        text: AppController.routeText(dialog.info, "note")
        wrapMode: Text.Wrap
    }
}
