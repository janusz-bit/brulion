pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// CodeOpenIn — "Open in…" of CodeView's header (docs/CODE.md "Open in an editor"): the button
// opens the folder in the preferred editor (CodeProjects.effectiveEditor; Ctrl+O in Code mode), the
// arrow beside it lists the editors of this computer (found on PATH), the custom command and the
// file manager — the one picked opens it and is remembered.
RowLayout {
    id: openIn

    property string folder
    readonly property list<var> editors: Workspace.projects.editors
    readonly property string current: Workspace.projects.effectiveEditor

    function nameOf(id: string): string {
        for (let i = 0; i < openIn.editors.length; ++i)
            if (AppController.routeText(openIn.editors[i], "id") === id)
                return AppController.routeText(openIn.editors[i], "name")
        return id
    }
    function open(editor: string): void {
        if (!Workspace.projects.openWith(editor, openIn.folder, -1))
            AppController.toast(qsTr("The editor could not be started: choose one in Settings → Extensions → Code mode."), "error")
    }

    spacing: 0

    BIconButton {
        objectName: "codeOpenEditor"
        iconName: openIn.current === "files" ? "folder-open" : "code"
        label: qsTr("Open in %1").arg(openIn.nameOf(openIn.current))
        shortcut: ShortcutMap.sequence("code.openInEditor")
        enabled: openIn.folder !== ""
        onClicked: openIn.open(openIn.current)
    }
    BIconButton {
        id: chooser
        objectName: "codeOpenIn"
        size: "small"
        iconName: "chevron-down"
        label: qsTr("Open in…")
        enabled: openIn.folder !== ""
        onClicked: {
            Workspace.projects.rescanEditors()
            menu.popup(chooser, 0, chooser.height)
        }
    }
    BMenu {
        id: menu
        objectName: "codeOpenInMenu"
        Instantiator {
            model: openIn.editors
            delegate: BMenuItem {
                id: item
                required property var modelData
                readonly property string editorId: AppController.routeText(item.modelData, "id")
                objectName: "codeOpenIn_" + item.editorId
                text: AppController.routeText(item.modelData, "name")
                iconName: item.editorId === openIn.current ? "check" : AppController.routeText(item.modelData, "icon")
                onTriggered: {
                    Workspace.projects.preferredEditor = item.editorId
                    openIn.open(item.editorId)
                }
            }
            onObjectAdded: (index, object) => menu.insertItem(index, object as Item)
            onObjectRemoved: (index, object) => menu.removeItem(object as Item)
        }
    }
}
