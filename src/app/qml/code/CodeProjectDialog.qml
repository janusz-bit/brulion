pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// CodeProjectDialog — a project's settings on this device (docs/CODE.md, docs/UI.md "Code mode"):
// its name, its icon (an emoji; without one the picture of its t3.json's iconPath, else a folder),
// the defaults of its new threads (the agent, where they work: the device's default, Local, New
// worktree) and its scripts (CodeScriptsEditor: this device's, then its t3.json's). Kept with the
// project on this device (CodeProjects.rename / setIcon / setDefaults / setScripts) — never in the
// project's folder.
BDialog {
    id: dialog

    property string projectId
    property string agent
    property string env
    readonly property list<var> agents: Workspace.chats.agentPartners()

    function openFor(id: string): void {
        dialog.projectId = id
        holder.map = Workspace.projects.project(id)
        nameField.text = Workspace.projects.nameOf(id)
        iconField.text = AppController.routeText(holder.map, "icon")
        dialog.agent = AppController.routeText(holder.map, "agent")
        dialog.env = AppController.routeText(holder.map, "env")
        scriptsEditor.reload()
        dialog.open()
    }
    function agentName(id: string): string {
        if (id === "")
            return qsTr("This device's default")
        const label = Workspace.chats.partnerLabel(id)
        return label !== "" ? label : id
    }

    objectName: "codeProjectDialog"
    title: qsTr("Project")
    subtitle: Workspace.projects.folderOf(dialog.projectId)
    iconName: "folder"
    preferredWidth: Math.min(720, Theme.windowWidth - 2 * Theme.space4)
    acceptText: qsTr("Save")
    acceptEnabled: Js.trim(nameField.text) !== ""
    initialFocusItem: nameField
    onAccepted: {
        Workspace.projects.rename(dialog.projectId, nameField.text)
        Workspace.projects.setIcon(dialog.projectId, iconField.text)
        Workspace.projects.setDefaults(dialog.projectId, dialog.agent, dialog.env)
        scriptsEditor.model.save()
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.space3
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.space1
            BText {
                text: qsTr("Name")
                variant: "label"
            }
            BTextField {
                id: nameField
                objectName: "codeProjectName"
                Layout.fillWidth: true
                accessibleName: qsTr("Name")
            }
        }
        ColumnLayout {
            spacing: Theme.space1
            BText {
                text: qsTr("Icon")
                variant: "label"
            }
            BTextField {
                id: iconField
                objectName: "codeProjectIconField"
                Layout.preferredWidth: 72
                placeholderText: "📁"
                accessibleName: qsTr("The project's icon: an emoji")
                maximumLength: 16
            }
        }
    }
    BText {
        Layout.fillWidth: true
        visible: AppController.routeText(Workspace.projects.project(dialog.projectId), "iconUrl") !== ""
        text: qsTr("Without an emoji, the picture its t3.json names (iconPath).")
        variant: "small"
        color: Theme.fgMuted
        wrapMode: Text.Wrap
    }

    BText {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space2
        text: qsTr("New threads")
        variant: "label"
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.space2
        BButton {
            id: agentButton
            objectName: "codeProjectAgent"
            size: "small"
            variant: "secondary"
            iconName: "sparkles"
            iconRight: false
            text: dialog.agentName(dialog.agent)
            Accessible.name: qsTr("The agent of new threads: %1").arg(agentButton.text)
            onClicked: agentMenu.popup(agentButton, 0, agentButton.height)
        }
        BSegmented {
            objectName: "codeProjectEnv"
            size: "small"
            accessibleName: qsTr("Where new threads work")
            model: [{ value: "", text: qsTr("Default") }, { value: "local", text: qsTr("Local") },
                    { value: "worktree", text: qsTr("New worktree") }]
            currentValue: dialog.env
            onActivated: (value) => dialog.env = "" + value
        }
    }
    BText {
        Layout.fillWidth: true
        text: dialog.env === "" ? qsTr("Default: Settings → Extensions → Code mode, else the project's t3.json, else Local.")
                                : qsTr("Each new thread can still choose in its header.")
        variant: "small"
        color: Theme.fgMuted
        wrapMode: Text.Wrap
    }
    MapHolder {
        id: holder
    }
    BMenu {
        id: agentMenu
        BMenuItem {
            text: qsTr("This device's default")
            checkable: true
            checked: dialog.agent === ""
            onTriggered: dialog.agent = ""
        }
        Instantiator {
            model: dialog.agents
            delegate: BMenuItem {
                id: agentItem
                required property var modelData
                readonly property string agentId: AppController.routeText(agentItem.modelData, "id")
                text: AppController.routeText(agentItem.modelData, "label")
                checkable: true
                checked: dialog.agent === agentItem.agentId
                onTriggered: dialog.agent = agentItem.agentId
            }
            onObjectAdded: (index, object) => agentMenu.insertItem(index + 1, object as Item)
            onObjectRemoved: (index, object) => agentMenu.removeItem(object as Item)
        }
    }

    BText {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space2
        text: qsTr("Scripts")
        variant: "label"
    }
    CodeScriptsEditor {
        id: scriptsEditor
        objectName: "codeProjectScripts"
        Layout.fillWidth: true
        project: dialog.projectId
    }
}
