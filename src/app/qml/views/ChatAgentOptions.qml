pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ChatAgentOptions — an AI agent's session options as chips (docs/UI.md "AI agents (ACP)";
// ChatView): its model, mode, effort … each a chip with its choice now, whose menu picks another
// (Workspace.chats.setAgentOption: set on the session and remembered for the agent's next ones); a
// bool a chip that toggles; then how much of the agent's context the conversation uses and what it
// cost. Shown for a conversation an agent answers — before its first message too, with the options
// the agent's last session offered.
Flow {
    id: bar

    /// The conversation ("" a new one) and who answers it.
    property string path
    property string partner

    readonly property WorkspaceChats chats: Workspace.chats
    readonly property list<var> options: bar.chats.agentOptions(bar.path, bar.partner, bar.chats.agentSerial)
    readonly property string usage: bar.path !== "" ? bar.chats.agentUsage(bar.path, bar.chats.agentSerial) : ""
    /// The option whose menu is open, and its value now.
    property string menuOption
    property string menuCurrent

    visible: bar.chats.isAgent(bar.partner) && (bar.options.length > 0 || bar.usage !== "")
    spacing: Theme.space1

    Repeater {
        model: bar.options
        delegate: BChip {
            id: chip
            required property var modelData
            readonly property string optionId: AppController.routeText(chip.modelData, "id")
            readonly property string optionName: AppController.routeText(chip.modelData, "name")
            readonly property bool boolean: AppController.routeFlag(chip.modelData, "boolean")
            readonly property string current: AppController.routeText(chip.modelData, "current")
            objectName: "chatAgentOption"
            size: "small"
            interactive: true
            showIcon: false
            selected: chip.boolean && chip.current === "true"
            text: chip.boolean ? chip.optionName : AppController.routeText(chip.modelData, "currentName")
            Accessible.name: chip.boolean ? chip.optionName : qsTr("%1: %2").arg(chip.optionName).arg(chip.text)
            onClicked: {
                if (chip.boolean) {
                    bar.chats.setAgentOption(bar.path, bar.partner, chip.optionId, chip.current === "true" ? "false" : "true")
                    return
                }
                bar.menuOption = chip.optionId
                bar.menuCurrent = chip.current
                choiceMenu.popup(chip, 0, chip.height)
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

    BMenu {
        id: choiceMenu
        objectName: "chatAgentChoices"
        onClosed: bar.menuOption = ""
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
                text: AppController.routeText(choice.modelData, "name")
                hint: choice.about !== "" ? choice.about : choice.group
                iconName: choice.value === bar.menuCurrent ? "check" : ""
                onTriggered: bar.chats.setAgentOption(bar.path, bar.partner, bar.menuOption, choice.value)
            }
            onObjectAdded: (index, object) => choiceMenu.insertItem(index, object as Item)
            onObjectRemoved: (index, object) => choiceMenu.removeItem(object as Item)
        }
    }
}
