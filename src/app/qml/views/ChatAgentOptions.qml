pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ChatAgentOptions — an AI agent's session options as chips (docs/UI.md "AI agents (ACP)";
// ChatView): each chip says what it sets and its choice now — "Model: Opus 5.5", "Effort: high",
// "Mode: auto" — with an icon of its kind and a tooltip on what it changes, so a bare "High" or
// "Off" never stands alone; its menu, titled by the option, picks another
// (Workspace.chats.setAgentOption: set on the session and remembered for the agent's next ones).
// A bool, and a select of just "Off" and "On" (Claude Code's "Fast mode"), is a switch chip: the
// option's name, highlighted while on, a click toggles it. Then how much of the agent's context the
// conversation uses and what it cost, and the subscription's limits of Claude Code and Codex
// (ChatAgentLimits). Shown for a conversation an agent answers — before its first message too, with
// the options the agent's last session offered.
// The option and value names come from the agent (in English): the known kinds (model, mode,
// effort, speed) and the common values (low … max, on, off, auto, the permission modes) are
// shown in the interface's language; others as the agent wrote them.
Flow {
    id: bar

    /// The conversation ("" a new one) and who answers it.
    property string path
    property string partner

    readonly property WorkspaceChats chats: Workspace.chats
    readonly property list<var> options: bar.chats.agentOptions(bar.path, bar.partner, bar.chats.agentSerial)
    readonly property string usage: bar.path !== "" ? bar.chats.agentUsage(bar.path, bar.chats.agentSerial) : ""
    /// The option whose menu is open, its name and its value now.
    property string menuOption
    property string menuTitle
    property string menuIcon
    property string menuCurrent

    /// What an option sets, in the interface's language for the kinds ACP names (`category`).
    function labelOf(category: string, name: string): string {
        switch (category) {
        case "model": return qsTr("Model")
        case "mode": return qsTr("Mode", "agent's permission mode")
        case "thought_level": return qsTr("Effort")
        case "speed": return qsTr("Fast mode")
        }
        switch (Js.toLowerCase(name)) {
        case "fast mode": return qsTr("Fast mode")
        case "effort": return qsTr("Effort")
        case "agent": return qsTr("Agent", "an AI agent's sub-agent option")
        }
        return name
    }
    /// The icon of an option's kind.
    function iconOf(category: string): string {
        switch (category) {
        case "model": return "cpu"
        case "mode": return "shield-check"
        case "thought_level": return "brain"
        case "speed": return "zap"
        }
        return "sliders-horizontal"
    }
    /// What an option changes (its tooltip): ours for the known kinds, else the agent's words.
    function aboutOf(category: string, description: string): string {
        switch (category) {
        case "model": return qsTr("Which model answers in this conversation")
        case "mode": return qsTr("What the agent may do without asking you first")
        case "thought_level": return qsTr("How long the model thinks before it answers: higher is slower and more thorough")
        case "speed": return qsTr("Faster answers, at a higher cost")
        }
        return description
    }
    /// A value's name in the interface's language when it is a common one; else as the agent wrote it.
    function valueName(name: string): string {
        switch (Js.toLowerCase(name)) {
        case "off": return qsTr("off")
        case "on": return qsTr("on")
        case "default": return qsTr("default")
        case "auto": return qsTr("auto")
        case "low": return qsTr("low", "effort")
        case "medium": return qsTr("medium", "effort")
        case "high": return qsTr("high", "effort")
        case "xhigh": case "extra high": return qsTr("extra high", "effort")
        case "max": case "maximum": return qsTr("max", "effort")
        case "manual": return qsTr("ask first")
        case "accept edits": return qsTr("accept edits")
        case "plan": return qsTr("plan only")
        case "bypass permissions": return qsTr("never ask")
        }
        return name
    }
    /// The same with a capital first letter (a menu's item).
    function capitalized(text: string): string {
        return Js.toUpperCase(Js.charAt(text, 0)) + Js.sliceFrom(text, 1)
    }

    readonly property bool limitsShown: bar.chats.isAgent(bar.partner) && bar.chats.limits.applies(bar.partner, bar.chats.limits.serial)
    visible: bar.chats.isAgent(bar.partner) && (bar.options.length > 0 || bar.usage !== "" || bar.limitsShown)
    spacing: Theme.space1

    Repeater {
        model: bar.options
        delegate: BChip {
            id: chip
            required property var modelData
            readonly property string optionId: AppController.routeText(chip.modelData, "id")
            readonly property string optionName: AppController.routeText(chip.modelData, "name")
            readonly property string category: AppController.routeText(chip.modelData, "category")
            readonly property string label: bar.labelOf(chip.category, chip.optionName)
            readonly property bool boolean: AppController.routeFlag(chip.modelData, "boolean")
            /// A select of "Off" and "On": a switch too.
            readonly property bool onOff: AppController.routeFlag(chip.modelData, "onOff")
            readonly property bool isSwitch: chip.boolean || chip.onOff
            readonly property bool isOn: chip.boolean ? chip.current === "true" : AppController.routeFlag(chip.modelData, "isOn")
            readonly property string current: AppController.routeText(chip.modelData, "current")
            readonly property string valueText: bar.valueName(AppController.routeText(chip.modelData, "currentName"))
            readonly property string about: bar.aboutOf(chip.category, AppController.routeText(chip.modelData, "description"))
            objectName: "chatAgentOption"
            size: "small"
            interactive: true
            iconName: bar.iconOf(chip.category)
            selected: chip.isSwitch && chip.isOn
            text: chip.isSwitch ? chip.label : qsTr("%1: %2").arg(chip.label).arg(chip.valueText)
            Accessible.name: chip.isSwitch ? qsTr("%1: %2").arg(chip.label).arg(chip.isOn ? qsTr("on") : qsTr("off"))
                                           : chip.text
            Accessible.description: chip.about
            onClicked: {
                if (chip.boolean) {
                    bar.chats.setAgentOption(bar.path, bar.partner, chip.optionId, chip.isOn ? "false" : "true")
                    return
                }
                if (chip.onOff) {
                    bar.chats.setAgentOption(bar.path, bar.partner, chip.optionId,
                                             AppController.routeText(chip.modelData, chip.isOn ? "offValue" : "onValue"))
                    return
                }
                bar.menuOption = chip.optionId
                bar.menuTitle = chip.label
                bar.menuIcon = chip.iconName
                bar.menuCurrent = chip.current
                choiceMenu.popup(chip, 0, chip.height)
            }

            // What it changes (and a switch's state), on hover or a long press.
            Loader {
                active: chip.about !== "" && (chip.hovered || chip.pressed)
                sourceComponent: BToolTip {
                    parent: chip
                    text: chip.isSwitch ? qsTr("%1 — now %2").arg(chip.about).arg(chip.isOn ? qsTr("on") : qsTr("off"))
                                        : chip.about
                    visible: chip.hovered && !chip.pressed
                }
            }
        }
    }
    BText {
        objectName: "chatAgentUsage"
        visible: bar.usage !== ""
        // (As tall as a small chip with its touch target: centred beside them.)
        height: Theme.touchUi ? Math.max(24, Theme.touchTarget) : 20
        verticalAlignment: Text.AlignVCenter
        text: bar.usage
        variant: "faint"
        font.pixelSize: Theme.textSm
    }
    // The subscription's limits (Claude Code, Codex of this computer).
    ChatAgentLimits {
        partner: bar.limitsShown ? bar.partner : ""
    }

    BMenu {
        id: choiceMenu
        objectName: "chatAgentChoices"
        onClosed: bar.menuOption = ""
        // The option the menu chooses, as its first line.
        BMenuItem {
            objectName: "chatAgentChoicesTitle"
            text: bar.menuTitle
            iconName: bar.menuIcon
            enabled: false
        }
        Instantiator {
            // (No option: no choices.)
            model: bar.chats.agentChoices(bar.path, bar.partner, bar.menuOption, bar.chats.agentSerial)
            delegate: BMenuItem {
                id: choice
                required property var modelData
                readonly property string value: AppController.routeText(choice.modelData, "value")
                readonly property string group: AppController.routeText(choice.modelData, "group")
                readonly property string about: AppController.routeText(choice.modelData, "description")
                objectName: "chatAgentChoice"
                text: bar.capitalized(bar.valueName(AppController.routeText(choice.modelData, "name")))
                hint: choice.about !== "" ? choice.about : choice.group
                iconName: choice.value === bar.menuCurrent ? "check" : ""
                onTriggered: bar.chats.setAgentOption(bar.path, bar.partner, bar.menuOption, choice.value)
            }
            // (After the title.)
            onObjectAdded: (index, object) => choiceMenu.insertItem(index + 1, object as Item)
            onObjectRemoved: (index, object) => choiceMenu.removeItem(object as Item)
        }
    }
}
