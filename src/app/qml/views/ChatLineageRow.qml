pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// ChatLineageRow — where a conversation came from and the sub-conversations AI agents started from
// it (docs/UI.md "Agents delegate"; ChatView, under the conversation's header): a sub-conversation
// shows the conversation it was started from as a chip ("From: Plan wyjazdu"); a conversation whose
// agent gave other partners tasks shows each of them as a chip — its name, who answers, and its
// state as an icon (a spinner while it runs, a question waiting, a failure), "↩" when the end of its
// turn will wake this conversation (an agent started it with wake) — and a click opens it
// (opened(path)). Workspace.chats.lineage tells them, read again when one changes.
Flow {
    id: row

    /// The conversation shown ("" a new one: nothing).
    property string path

    signal opened(string path)

    readonly property ChatLineage lineage: Workspace.chats.lineage
    readonly property list<var> children: row.path !== "" ? row.lineage.children(row.path, row.lineage.serial) : row.none
    readonly property string parentPath: row.path !== "" && row.lineage.serial >= 0 ? row.lineage.parentOf(row.path) : ""
    readonly property list<var> none: []

    /// The state of a sub-conversation in words (its chip's tooltip and accessible name).
    function stateText(state: string, failure: string): string {
        switch (state) {
        case "running": return qsTr("answering…")
        case "queued": return qsTr("waiting for the model on this device")
        case "asking": return qsTr("waits for your answer")
        case "failed": return qsTr("failed: %1").arg(failure)
        }
        return qsTr("answered")
    }
    function stateIcon(state: string): string {
        switch (state) {
        case "running": return "refresh-cw"
        case "queued": return "hourglass"
        case "asking": return "circle-help"
        case "failed": return "circle-alert"
        }
        return "circle-check"
    }

    visible: row.parentPath !== "" || row.children.length > 0
    spacing: Theme.space1

    BChip {
        objectName: "chatParent"
        visible: row.parentPath !== ""
        size: "small"
        interactive: true
        outlined: false
        iconName: "arrow-up"
        text: qsTr("From: %1").arg(Workspace.chats.nameOf(row.parentPath))
        Accessible.name: qsTr("Started from the conversation %1").arg(Workspace.chats.nameOf(row.parentPath))
        onClicked: row.opened(row.parentPath)
    }
    BText {
        visible: row.children.length > 0
        // (As tall as a small chip with its touch target: centred beside them.)
        height: Theme.touchUi ? Math.max(24, Theme.touchTarget) : 20
        verticalAlignment: Text.AlignVCenter
        text: qsTr("Sub-conversations:")
        variant: "faint"
        font.pixelSize: Theme.textSm
    }
    Repeater {
        model: row.children
        delegate: BChip {
            id: chip
            required property var modelData
            readonly property string childPath: AppController.routeText(chip.modelData, "path")
            readonly property string partnerName: AppController.routeText(chip.modelData, "partner")
            readonly property string childState: AppController.routeText(chip.modelData, "state")
            readonly property bool wakes: AppController.routeFlag(chip.modelData, "wakes")
            readonly property string about: row.stateText(chip.childState, AppController.routeText(chip.modelData, "failure"))
                                            + (chip.wakes ? " · " + qsTr("will wake this conversation when its turn ends") : "")
            objectName: "chatSubConversation"
            size: "small"
            interactive: true
            outlined: true
            iconName: row.stateIcon(chip.childState)
            variant: chip.childState === "failed" ? "overdue" : "neutral"
            selected: chip.childState === "asking"
            text: qsTr("%1 · %2").arg(AppController.routeText(chip.modelData, "name")).arg(chip.partnerName) + (chip.wakes ? " ↩" : "")
            Accessible.name: qsTr("Sub-conversation %1 with %2: %3").arg(AppController.routeText(chip.modelData, "name"))
                                                                     .arg(chip.partnerName).arg(chip.about)
            onClicked: row.opened(chip.childPath)

            Loader {
                active: chip.hovered || chip.pressed
                sourceComponent: BToolTip {
                    parent: chip
                    text: qsTr("%1 — %2").arg(chip.partnerName).arg(chip.about)
                    visible: chip.hovered && !chip.pressed
                }
            }
        }
    }
}
