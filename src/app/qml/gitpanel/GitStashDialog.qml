pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// GitStashDialog — the changes panel's stashes: "Stash changes" with a message (new files too:
// `git stash push --include-untracked`) and the repository's stashes (CodeGit.stashList: shared by
// all its worktrees, the newest first) with Apply, Pop (applied, then dropped) and Drop (the owner
// asks first: dropRequested). A stash is named by its commit, not by its place in the list, so one
// pushed meanwhile by another worktree never makes Drop take the wrong one.
//
//   GitStashDialog { id: stashDialog; git: panel.git; onDropRequested: (oid, message) => … }
BDialog {
    id: dialog
    objectName: "gitStashDialog"

    property CodeGit git: null
    readonly property list<var> noRows: []

    signal dropRequested(string oid, string message)

    function openFor(): void {
        messageField.text = ""
        if (dialog.git !== null)
            dialog.git.stashes()
        dialog.open()
    }

    title: qsTr("Stashes")
    subtitle: qsTr("Changes put aside: the working tree goes back to the last commit until a stash is applied.")
    iconName: "inbox"
    acceptText: qsTr("Stash changes")
    acceptEnabled: dialog.git !== null && !dialog.git.busy && dialog.git.changeCount > 0
    rejectText: qsTr("Close")
    initialFocusItem: messageField
    onAccepted: dialog.git.stashPush(messageField.text)

    BTextField {
        id: messageField
        objectName: "gitStashMessage"
        Layout.fillWidth: true
        enabled: dialog.git !== null && dialog.git.changeCount > 0
        accessibleName: qsTr("Stash message")
        placeholderText: dialog.git !== null && dialog.git.changeCount > 0 ? qsTr("What it holds (optional)")
                                                                          : qsTr("No changes to stash")
        onAccepted: dialog.tryAccept()
    }
    BText {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space2
        text: qsTr("Stashes")
        variant: "label"
    }
    BText {
        objectName: "gitNoStashes"
        Layout.fillWidth: true
        visible: dialog.git === null || dialog.git.stashList.length === 0
        text: qsTr("None yet.")
        variant: "faint"
    }
    ListView {
        id: stashList
        objectName: "gitStashList"
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(contentHeight, 240)
        visible: count > 0
        clip: true
        model: dialog.git !== null ? dialog.git.stashList : dialog.noRows
        boundsBehavior: Flickable.StopAtBounds
        T.ScrollBar.vertical: BScrollBar {}
        // CodeGit.stashList: [{index, oid, message, branch, time, when}]
        delegate: RowLayout {
            id: stash
            required property string oid
            required property string message
            required property string when
            required property int index
            objectName: "gitStash_" + stash.index
            width: ListView.view.width
            spacing: Theme.space1
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                BText {
                    Layout.fillWidth: true
                    text: stash.message
                    elide: Text.ElideRight
                }
                BText {
                    Layout.fillWidth: true
                    text: stash.when
                    variant: "faint"
                    font.pixelSize: Theme.textXs
                }
            }
            BButton {
                objectName: "gitStashApply"
                text: qsTr("Apply")
                size: "small"
                variant: "ghost"
                enabled: dialog.git !== null && !dialog.git.busy
                onClicked: dialog.git.stashApply(stash.oid)
            }
            BButton {
                objectName: "gitStashPop"
                text: qsTr("Pop")
                size: "small"
                variant: "ghost"
                enabled: dialog.git !== null && !dialog.git.busy
                onClicked: dialog.git.stashPop(stash.oid)
            }
            BIconButton {
                objectName: "gitStashDrop"
                iconName: "trash-2"
                label: qsTr("Drop")
                size: "small"
                enabled: dialog.git !== null && !dialog.git.busy
                onClicked: dialog.dropRequested(stash.oid, stash.message)
            }
        }
    }
}
