pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// AgentUsageButton — "Usage…" on the row of an AI agent of this computer that is Claude Code or
// Codex (Settings → Extensions → AI agents; docs/UI.md "AI agents (ACP)", "Its usage";
// Workspace.chats.usage, shell/agentusage.h). Hidden for any other agent (and where no programs
// run). While shown it reads the agent's session logs — at most every five minutes per log folder,
// again every five minutes while it stays shown — and gives the row its line, `summary` ("Today
// 1.2 M tokens · ≈ 4.10 USD · 30 days ≈ 61 USD"); a click asks for the dialog
// (AgentUsageDialog.qml, openRequested()).
BButton {
    id: button

    /// The agent ("acp:<id>"; "" none).
    property string partner

    readonly property AgentUsage usage: Workspace.chats.usage
    readonly property var info: button.usage.state(button.partner, button.usage.serial)
    readonly property bool applies: AppController.routeFlag(button.info, "applies")
    /// The row's line ("" before the first reading, and for other agents).
    readonly property string summary: button.applies ? AppController.routeText(button.info, "summary") : ""

    signal openRequested(string partner)

    function look(): void {
        if (button.visible && button.partner !== "")
            button.usage.show(button.partner)
    }
    // (After the bindings that showed it: a reading changes `serial`, which they read.)
    Later {
        id: lookLater
        onTriggered: button.look()
    }
    onVisibleChanged: lookLater.restart()
    onPartnerChanged: lookLater.restart()
    Component.onCompleted: lookLater.restart()
    Timer {
        interval: 5 * 60 * 1000 + 1000
        repeat: true
        running: button.visible
        onTriggered: button.look()
    }

    objectName: "acpAgentUsage"
    visible: button.applies && button.partner !== ""
    size: "small"
    variant: "ghost"
    iconName: "chart-column"
    busy: AppController.routeFlag(button.info, "busy")
    text: qsTr("Usage…")
    Accessible.description: button.summary
    onClicked: button.openRequested(button.partner)
}
