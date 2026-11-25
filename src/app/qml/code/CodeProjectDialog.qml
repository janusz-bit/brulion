pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// CodeProjectDialog — a project's name and its script buttons (docs/CODE.md): one line per script,
// "name: command" ("dev: npm run dev", "tests: ctest --output-on-failure"); CodeView shows them as
// buttons that run the command in the thread's folder in a terminal of its panel. Kept with the
// project on this device (CodeProjects.setScripts).
BDialog {
    id: dialog

    property string projectId

    function openFor(id: string): void {
        dialog.projectId = id
        nameField.text = Workspace.projects.nameOf(id)
        scriptsField.text = Workspace.projects.scriptsText(id)
        dialog.open()
    }

    objectName: "codeProjectDialog"
    title: qsTr("Project")
    subtitle: Workspace.projects.folderOf(dialog.projectId)
    iconName: "folder"
    acceptText: qsTr("Save")
    acceptEnabled: Js.trim(nameField.text) !== ""
    initialFocusItem: nameField
    onAccepted: {
        Workspace.projects.rename(dialog.projectId, nameField.text)
        Workspace.projects.setScriptsText(dialog.projectId, scriptsField.text)
    }

    BText {
        Layout.fillWidth: true
        text: qsTr("Name")
        variant: "label"
    }
    BTextField {
        id: nameField
        objectName: "codeProjectName"
        Layout.fillWidth: true
        accessibleName: qsTr("Name")
    }
    BText {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space2
        text: qsTr("Scripts")
        variant: "label"
    }
    BText {
        Layout.fillWidth: true
        text: qsTr("A line each, “name: command” — a button over the thread runs it in a terminal of the thread's folder.")
        variant: "small"
        color: Theme.fgMuted
        wrapMode: Text.Wrap
    }
    BTextArea {
        id: scriptsField
        objectName: "codeProjectScripts"
        Layout.fillWidth: true
        Layout.preferredHeight: 120
        placeholderText: "dev: npm run dev\ntest: npm test"
        monospace: true
        accessibleName: qsTr("Scripts")
    }
}
