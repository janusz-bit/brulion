pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// CodeRemoteChanges — the changes of a thread whose project is on another computer of the user's
// (docs/CODE.md "Threads on the phone"), in place of the Changes panel: the computer has the
// project, its git and its terminal, so this side reads what it says (Workspace.projects.remote:
// RemoteProjects.requestChanges / requestDiff over its `acp serve`) — the files the agent's last
// turn changed there (since the turn's checkpoint), or every change not committed, read-only, and
// a file's diff (BDiffView: beside the list when wide, in its place when narrow). Committing, pushing
// and the terminal stay that computer's (said in the header). Refreshed when the checkpoint changes,
// when the owner asks (refresh(): a turn ended) and with its button.
//
//   CodeRemoteChanges { project: id; worktree: place.worktree; checkpoint: lastCheckpoint; host: place.host }
Rectangle {
    id: panel
    objectName: "codeRemoteChanges"

    /// The project's id there, the thread's worktree there ("" the project's folder), the turn's
    /// checkpoint ("" none: all changes), the computer's name.
    property string project
    property string worktree
    property string checkpoint
    property string host
    /// "turn" | "all" — what the list shows while there is a checkpoint.
    property string mode: "turn"

    readonly property bool turnMode: panel.checkpoint !== "" && panel.mode === "turn"
    readonly property bool wide: panel.width >= 760
    readonly property bool showDiff: panel.selectedPath !== ""

    property list<var> rows
    property bool loading: false
    property bool loaded: false
    property string error
    property bool truncated: false
    property int changesSerial: 0

    property string selectedPath
    property string selectedOldPath
    property string diffOld
    property string diffNew
    property bool diffBinary: false
    property bool diffTooLarge: false
    property bool diffLoading: false
    property string diffError
    property int diffSerial: 0

    readonly property list<var> modes: [
        { value: "turn", text: qsTr("This turn"), icon: "history" },
        { value: "all", text: qsTr("All changes"), icon: "git-compare" }
    ]

    function refresh(): void {
        const remote = Workspace.projects.remote
        if (remote === null || panel.project === "")
            return
        panel.loading = true
        panel.changesSerial = remote.requestChanges(panel.project, panel.worktree, panel.turnMode ? panel.checkpoint : "")
    }
    function openDiff(path: string, oldPath: string): void {
        const remote = Workspace.projects.remote
        if (remote === null)
            return
        panel.selectedPath = path
        panel.selectedOldPath = oldPath
        panel.diffOld = ""
        panel.diffNew = ""
        panel.diffBinary = false
        panel.diffTooLarge = false
        panel.diffError = ""
        panel.diffLoading = true
        panel.diffSerial = remote.requestDiff(panel.project, panel.worktree, panel.turnMode ? panel.checkpoint : "",
                                              path, oldPath)
    }
    function closeDiff(): void {
        panel.selectedPath = ""
        panel.selectedOldPath = ""
        panel.diffLoading = false
        panel.diffError = ""
    }
    function setMode(value: string): void {
        panel.mode = value
        panel.closeDiff()
        again.restart()
    }

    onProjectChanged: {
        panel.closeDiff()
        again.restart()
    }
    onWorktreeChanged: again.restart()
    onCheckpointChanged: again.restart()
    onVisibleChanged: {
        if (panel.visible)
            again.restart()
    }
    Component.onCompleted: again.restart()

    Later {
        id: again
        onTriggered: panel.refresh()
    }
    Connections {
        target: Workspace.projects.remote
        function onChangesReady(serial: int, changes: list<var>, truncated: bool, error: string) {
            if (serial !== panel.changesSerial)
                return
            panel.loading = false
            panel.loaded = true
            panel.error = error
            panel.truncated = truncated
            panel.rows = error === "" ? changes : []
        }
        function onDiffReady(serial: int, path: string, oldText: string, newText: string, binary: bool,
                             tooLarge: bool, error: string) {
            if (serial !== panel.diffSerial || path !== panel.selectedPath)
                return
            panel.diffLoading = false
            panel.diffError = error
            panel.diffOld = oldText
            panel.diffNew = newText
            panel.diffBinary = binary
            panel.diffTooLarge = tooLarge
        }
    }

    color: Theme.bg

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- Where it is, refresh ----------------------------------------------------------
        RowLayout {
            objectName: "codeRemoteHeader"
            Layout.fillWidth: true
            Layout.leftMargin: Theme.space3
            Layout.rightMargin: Theme.space2
            Layout.topMargin: Theme.space2
            Layout.bottomMargin: Theme.space1
            spacing: Theme.space2
            BIcon {
                name: "laptop"
                size: Theme.iconSize
                color: Theme.fgMuted
            }
            BText {
                objectName: "codeRemoteNote"
                Layout.fillWidth: true
                text: qsTr("Changes and the terminal are on %1: here they are read-only.").arg(panel.host)
                variant: "small"
                color: Theme.fgMuted
                wrapMode: Text.Wrap
            }
            BSpinner {
                visible: panel.loading
                size: Theme.iconSizeSm
            }
            BIconButton {
                objectName: "codeRemoteRefresh"
                iconName: "refresh-cw"
                label: qsTr("Refresh")
                size: "small"
                onClicked: panel.refresh()
            }
        }

        // ---- This turn / All changes -----------------------------------------------------------
        BSegmented {
            objectName: "codeRemoteMode"
            Layout.leftMargin: Theme.space2
            Layout.bottomMargin: Theme.space2
            visible: panel.checkpoint !== ""
            size: "small"
            accessibleName: qsTr("Which changes")
            model: panel.modes
            currentValue: panel.mode
            onActivated: (value) => panel.setMode("" + value)
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.borderSubtle
        }

        // ---- The files and a file's diff ----------------------------------------------------------
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Item {
                id: listPane
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                width: panel.showDiff && panel.wide ? Math.min(360, Math.round(parent.width * 0.38)) : parent.width
                visible: !panel.showDiff || panel.wide

                ListView {
                    id: list
                    objectName: "codeRemoteChangeList"
                    anchors.fill: parent
                    anchors.margins: Theme.space1
                    clip: true
                    model: panel.rows
                    boundsBehavior: Flickable.StopAtBounds
                    T.ScrollBar.vertical: BScrollBar {}
                    Accessible.role: Accessible.List
                    Accessible.name: qsTr("Changed files")
                    delegate: ChangeRow {}
                    footer: BText {
                        width: list.width
                        visible: panel.truncated
                        height: visible ? implicitHeight + Theme.space2 : 0
                        leftPadding: Theme.space3
                        topPadding: Theme.space2
                        text: qsTr("and more files")
                        variant: "faint"
                        font.pixelSize: Theme.textSm
                    }
                }
                BEmptyState {
                    objectName: "codeRemoteNoChanges"
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 2 * Theme.space4, maximumWidth)
                    visible: panel.loaded && panel.error === "" && list.count === 0
                    compact: true
                    iconName: "circle-check"
                    title: panel.turnMode ? qsTr("No changes in this turn") : qsTr("No changes")
                    description: panel.turnMode ? qsTr("The files are as they were before the turn.")
                               : qsTr("The working tree matches the last commit.")
                }
                BEmptyState {
                    objectName: "codeRemoteError"
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 2 * Theme.space4, maximumWidth)
                    visible: panel.error !== ""
                    compact: true
                    iconName: "cloud-off"
                    title: qsTr("The changes cannot be shown")
                    description: panel.error
                }
            }

            Item {
                id: diffPane
                objectName: "codeRemoteDiffPane"
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.right: parent.right
                anchors.left: panel.wide ? listPane.right : parent.left
                visible: panel.showDiff

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.space2
                    spacing: Theme.space2
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space1
                        BIconButton {
                            objectName: "codeRemoteDiffBack"
                            iconName: panel.wide ? "x" : "chevron-left"
                            label: panel.wide ? qsTr("Close the diff") : qsTr("Back to the files")
                            size: "small"
                            onClicked: panel.closeDiff()
                        }
                        BText {
                            Layout.fillWidth: true
                            text: panel.selectedPath
                            variant: "label"
                            elide: Text.ElideMiddle
                        }
                    }
                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        BDiffView {
                            objectName: "codeRemoteDiffView"
                            anchors.fill: parent
                            visible: !panel.diffLoading && panel.diffError === "" && !panel.diffBinary && !panel.diffTooLarge
                            oldText: panel.diffOld
                            newText: panel.diffNew
                            oldLabel: panel.turnMode ? qsTr("Before the turn") : qsTr("Last commit")
                            newLabel: qsTr("Now")
                            mode: panel.wide ? "auto" : "unified"
                        }
                        BSpinner {
                            anchors.centerIn: parent
                            visible: panel.diffLoading
                        }
                        BText {
                            anchors.centerIn: parent
                            width: parent.width - 2 * Theme.space4
                            visible: !panel.diffLoading && (panel.diffError !== "" || panel.diffBinary || panel.diffTooLarge)
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            color: Theme.fgMuted
                            text: panel.diffError !== "" ? panel.diffError
                                  : panel.diffBinary ? qsTr("A binary file: no diff to show.")
                                  : qsTr("Too big to show here.")
                        }
                    }
                }
            }
        }
    }

    // ---- A changed file ----------------------------------------------------------------------
    component ChangeRow: T.ItemDelegate {
        id: row
        required property string path
        required property string oldPath
        required property string letter
        required property string name
        required property string dir
        required property int additions
        required property int deletions
        required property bool binary
        readonly property bool selected: row.path === panel.selectedPath

        width: ListView.view.width
        implicitHeight: Theme.touchUi ? 44 : 32
        objectName: "codeRemoteChange"
        Accessible.name: row.path
        onClicked: panel.openDiff(row.path, row.oldPath)

        background: Rectangle {
            radius: Theme.radius
            color: row.selected ? Theme.bgSelected : row.hovered ? Theme.bgHover : "transparent"
        }
        contentItem: RowLayout {
            spacing: Theme.space2
            BText {
                Layout.leftMargin: Theme.space2
                Layout.preferredWidth: Theme.space4
                text: row.letter
                font.weight: Theme.weightSemiBold
                color: row.letter === "A" || row.letter === "U" ? Theme.successText
                       : row.letter === "D" || row.letter === "C" ? Theme.dangerText
                       : row.letter === "R" ? Theme.infoText : Theme.warningText
            }
            BText {
                text: row.name
                elide: Text.ElideRight
                Layout.maximumWidth: row.width * 0.6
            }
            BText {
                Layout.fillWidth: true
                text: row.dir
                variant: "faint"
                font.pixelSize: Theme.textSm
                elide: Text.ElideLeft
            }
            BText {
                Layout.rightMargin: Theme.space2
                visible: !row.binary && (row.additions > 0 || row.deletions > 0)
                text: "+" + row.additions + " −" + row.deletions
                variant: "faint"
                font.pixelSize: Theme.textSm
            }
        }
    }
}
