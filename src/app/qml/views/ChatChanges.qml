pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ChatChanges — the files of the notebook an AI agent's last turn changed, under its answer (docs/UI.md
// "AI agents (ACP)" → "What a turn changed"; as T3 Code's turn diff): a line "Changed 3 files" with
// Undo and ×, then a row per file — its name (a tap opens it), what happened to it (changed, new,
// deleted) and "Show the change" (a unified diff of its text before the turn and now). A file whose
// text before is not known (the agent changed it before the app saw its call) says so and is left as
// it is by Undo. WorkspaceChats.changes() / undoChanges() / forgetChanges().
Rectangle {
    id: card

    /// {path, oldText, newText, existed, exists, known} of each file.
    property list<var> changes
    readonly property int unknownCount: card.countUnknown()

    signal openFile(string file)
    signal undo()
    signal dismiss()

    function countUnknown(): int {
        let n = 0
        for (let i = 0; i < card.changes.length; ++i)
            n += AppController.routeFlag(card.changes[i], "known") ? 0 : 1
        return n
    }

    implicitHeight: column.implicitHeight + 2 * Theme.space2
    radius: Theme.radiusLg
    color: Theme.bgElevated
    border.color: Theme.borderSubtle

    ColumnLayout {
        id: column
        x: Theme.space3
        y: Theme.space2
        width: parent.width - 2 * Theme.space3
        spacing: Theme.space1

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space2
            BIcon {
                name: "file-diff"
                size: Theme.iconSizeSm
                color: Theme.fgMuted
            }
            BText {
                objectName: "chatChangesTitle"
                Layout.fillWidth: true
                text: qsTr("Changed %n file(s)", "", card.changes.length)
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            BButton {
                objectName: "chatChangesUndo"
                size: "small"
                variant: "secondary"
                iconName: "undo-2"
                text: qsTr("Undo these changes")
                enabled: card.unknownCount < card.changes.length
                onClicked: card.undo()
            }
            BIconButton {
                objectName: "chatChangesDismiss"
                size: "small"
                label: qsTr("Keep them")
                iconName: "x"
                onClicked: card.dismiss()
            }
        }

        Repeater {
            model: card.changes
            delegate: ColumnLayout {
                id: change
                required property var modelData
                readonly property string file: AppController.routeText(change.modelData, "path")
                readonly property bool existed: AppController.routeFlag(change.modelData, "existed")
                readonly property bool exists: AppController.routeFlag(change.modelData, "exists")
                readonly property bool known: AppController.routeFlag(change.modelData, "known")
                /// Its change is shown.
                property bool diffOpen: false
                objectName: "chatChange"
                Layout.fillWidth: true
                spacing: 2
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.space2
                    BIcon {
                        name: !change.exists ? "trash-2" : !change.existed ? "file-output" : "pencil"
                        size: 14
                        color: Theme.fgMuted
                    }
                    BText {
                        Layout.fillWidth: true
                        text: change.file
                        textFormat: Text.PlainText
                        color: Theme.link
                        font.pixelSize: Theme.textSm
                        elide: Text.ElideMiddle
                        TapHandler {
                            enabled: change.exists
                            onTapped: card.openFile(change.file)
                        }
                        HoverHandler {
                            enabled: change.exists && !Theme.touchUi
                            cursorShape: Qt.PointingHandCursor
                        }
                    }
                    BText {
                        text: !change.known ? qsTr("not known before")
                              : !change.exists ? qsTr("deleted")
                              : !change.existed ? qsTr("new") : qsTr("changed")
                        variant: "faint"
                        font.pixelSize: Theme.textSm
                    }
                }
                BText {
                    objectName: "chatChangeDiffToggle"
                    Layout.leftMargin: 14 + Theme.space2
                    visible: change.known
                    text: change.diffOpen ? qsTr("Hide the change") : qsTr("Show the change")
                    color: Theme.accentText
                    font.pixelSize: Theme.textSm
                    TapHandler {
                        onTapped: change.diffOpen = !change.diffOpen
                    }
                }
                BDiffView {
                    id: diff
                    objectName: "chatChangeDiff"
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(260, diff.rows.length * 24 + 8)
                    visible: change.diffOpen
                    mode: "unified"
                    showToolbar: false
                    oldText: change.diffOpen ? AppController.routeText(change.modelData, "oldText") : ""
                    newText: change.diffOpen ? AppController.routeText(change.modelData, "newText") : ""
                    oldLabel: change.file
                    newLabel: change.file
                }
            }
        }
    }
}
