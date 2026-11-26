pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ChatComputerList — what the desktop app on the user's other computers is doing (docs/CODE.md "The
// computer's own turns on the phone"; Workspace.chats.computerChats): each conversation whose turn
// runs, waits or asks there — or ended there lately — with the computer's name and what it does or
// asks; a row opens it (`opened`), where it is followed live and steered: its question answered, Stop,
// the next message. Shown above ChatView's list of conversations and in Code mode's sidebar, only
// while there is one.
ColumnLayout {
    id: list

    /// `path`'s conversation (a thread of Code mode when `project` is set).
    signal opened(string path, string project)

    readonly property list<var> rows: Workspace.chats.computerChats(Workspace.chats.computerSerial)
    /// The conversation shown (its row marked).
    property string current

    objectName: "chatComputers"
    visible: list.rows.length > 0
    spacing: 0

    BText {
        Layout.fillWidth: true
        Layout.leftMargin: Theme.space3
        Layout.rightMargin: Theme.space3
        Layout.topMargin: Theme.space1
        Layout.bottomMargin: Theme.space1
        text: qsTr("On your computers")
        variant: "caption"
        color: Theme.fgFaint
        font.weight: Font.DemiBold
        elide: Text.ElideRight
    }

    Repeater {
        model: list.rows
        delegate: BListItem {
            id: row
            required property var modelData
            readonly property string path: AppController.routeText(row.modelData, "path")
            readonly property string chatState: AppController.routeText(row.modelData, "state")
            readonly property string host: AppController.routeText(row.modelData, "host")
            readonly property string question: AppController.routeText(row.modelData, "question")
            readonly property string status: AppController.routeText(row.modelData, "status")
            readonly property bool running: row.chatState === "running" || row.chatState === "queued"
            objectName: "chatComputer_" + AppController.routeText(row.modelData, "name")
            Layout.fillWidth: true
            text: AppController.routeText(row.modelData, "name")
            subtitle: row.chatState === "asking" ? qsTr("Asks on %1: %2").arg(row.host).arg(row.question)
                      : row.chatState === "failed" ? qsTr("Failed on %1").arg(row.host)
                      : row.chatState === "idle" ? qsTr("Finished on %1").arg(row.host)
                      : row.chatState === "queued" ? qsTr("Waiting on %1").arg(row.host)
                      : row.status !== "" ? qsTr("On %1: %2").arg(row.host).arg(row.status)
                      : qsTr("Working on %1").arg(row.host)
            iconName: row.chatState === "asking" ? "circle-help"
                      : row.chatState === "failed" ? "circle-alert"
                      : row.chatState === "idle" ? "check"
                      : "laptop"
            iconColor: row.chatState === "asking" ? Theme.accent : row.chatState === "failed" ? Theme.dangerText : Theme.fgMuted
            subtitleColor: row.chatState === "failed" ? Theme.dangerText : Theme.fgMuted
            strong: row.chatState === "asking"
            dense: true
            selected: row.path === list.current
            trailing: BSpinner {
                visible: row.running
                running: row.running && row.visible
                size: Theme.iconSizeSm
                calm: true
            }
            onClicked: list.opened(row.path, AppController.routeText(row.modelData, "project"))
        }
    }
}
