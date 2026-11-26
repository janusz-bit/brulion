pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// CodeThreadTitle — the thread's name in CodeView's header, editable as in T3 Code: a double
// click, F2 (the command code.renameThread) or the sidebar's Rename edit it in place; Enter renames
// the thread (WorkspaceChats.rename: its page, its date kept in front), Esc or leaving it keeps
// the name. A new thread shows "New thread".
Item {
    id: title

    property string path
    property string text
    property bool editing: false

    function edit(): void {
        if (title.path === "")
            return
        field.text = title.text
        title.editing = true
        field.forceActiveFocus()
        field.selectAll()
    }
    function commit(): void {
        if (!title.editing)
            return
        title.editing = false
        const name = Js.trim(field.text)
        if (name === "" || name === title.text)
            return
        const renamed = Workspace.chats.rename(title.path, name)
        if (renamed === "")
            AppController.toast(qsTr("The thread could not be renamed: another one has that name."), "error")
        else
            AppController.replaceRoute({ view: "code", path: renamed })
    }

    objectName: "codeThreadTitle"
    implicitWidth: title.editing ? 260 : label.implicitWidth
    implicitHeight: Math.max(label.implicitHeight, field.implicitHeight)
    Layout.maximumWidth: 420
    onPathChanged: title.editing = false

    BText {
        id: label
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width
        visible: !title.editing
        text: title.text
        font.weight: Theme.weightSemiBold
        elide: Text.ElideRight
        Accessible.role: title.path !== "" ? Accessible.Button : Accessible.StaticText
        Accessible.name: title.path !== "" ? qsTr("Thread: %1 (double click to rename)").arg(title.text) : title.text
        TapHandler {
            enabled: title.path !== ""
            onDoubleTapped: title.edit()
        }
        HoverHandler {
            id: hover
            enabled: title.path !== "" && !Theme.touchUi
            cursorShape: Qt.IBeamCursor
        }
        BToolTip {
            visible: hover.hovered
            text: qsTr("Double click to rename")
            shortcut: ShortcutMap.sequence("code.renameThread")
        }
    }
    BTextField {
        id: field
        objectName: "codeThreadTitleField"
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width
        visible: title.editing
        size: "small"
        accessibleName: qsTr("The thread's name")
        onAccepted: title.commit()
        onEscapePressed: title.editing = false
        onActiveFocusChanged: {
            if (!field.activeFocus)
                title.commit()
        }
        Keys.onEscapePressed: title.editing = false
    }
}
