pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// SyncLiveProgress — what the running Nextcloud sync is doing right now (Workspace.sync.live,
// at most four updates a second): the phase, "n of N", the files in flight, how long it runs,
// the bytes moved, and a clear warning when the server stops answering. Shown only while a
// sync runs.
//
//   SyncLiveProgress { Layout.fillWidth: true; onShowLog: AppController.navigate({ view: "logs", group: "sync" }) }
ColumnLayout {
    id: live

    readonly property WorkspaceSync sync: Workspace.sync
    readonly property bool running: !!sync && sync.live.running === true

    /// The files the stalled run waits for.
    function waitingFor(): list<string> {
        if (!live.running)
            return []
        return live.sync.live.waitingFor
    }
    /// The non-empty parts joined with " · ".
    function joinNonEmpty(parts: list<string>): string {
        let out = ""
        for (let i = 0; i < parts.length; ++i) {
            if (parts[i] !== "")
                out = out === "" ? parts[i] : out + " · " + parts[i]
        }
        return out
    }

    signal showLog()

    visible: running
    spacing: Theme.space2

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.space2
        BSpinner {
            Layout.alignment: Qt.AlignVCenter
            running: live.running && !live.sync.live.stalled
            visible: running
        }
        BIcon {
            Layout.alignment: Qt.AlignVCenter
            visible: live.running && live.sync.live.stalled
            name: "triangle-alert"
            size: Theme.iconSize
            color: Theme.warningText
        }
        BText {
            Layout.fillWidth: true
            text: live.running ? live.sync.live.phaseText : ""
            variant: "label"
            wrapMode: Text.Wrap
            Accessible.role: Accessible.StaticText
            Accessible.name: text + (live.running && live.sync.live.detailText !== "" ? ", " + live.sync.live.detailText : "")
        }
        BText {
            visible: text !== "" && !Theme.compact
            text: live.running ? live.sync.live.detailText : ""
            variant: "small"
            color: Theme.fgMuted
        }
    }

    BText {
        Layout.fillWidth: true
        visible: Theme.compact && text !== ""
        text: live.running ? live.sync.live.detailText : ""
        variant: "small"
        color: Theme.fgMuted
        wrapMode: Text.Wrap
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.space3
        BProgressBar {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            indeterminate: !live.running || live.sync.live.progress < 0
            value: live.running && live.sync.live.progress >= 0 ? live.sync.live.progress : 0
            Accessible.name: qsTr("Sync progress")
        }
        // A long first sync over mobile data can be stopped: what is done stays synced.
        BButton {
            text: qsTr("Stop")
            iconName: "x"
            size: "small"
            enabled: live.running && !live.sync.live.abandoning
            onClicked: live.sync.cancelSync()
        }
    }

    // Elapsed · bytes · trigger, e.g. "Running for 1 min 20 s · 1.4 MB · after an edit".
    BText {
        Layout.fillWidth: true
        variant: "small"
        color: Theme.fgMuted
        wrapMode: Text.Wrap
        text: {
            if (!live.running)
                return ""
            const run = live.sync.live
            // The elapsed time always comes first.
            const rest = live.joinNonEmpty([run.bytesText, run.triggerText, run.number > 0 ? "#" + run.number : ""])
            const elapsed = "" + run.elapsedText
            return rest === "" ? elapsed : elapsed + " · " + rest
        }
    }

    BText {
        Layout.fillWidth: true
        visible: text !== ""
        text: live.running ? live.sync.live.activeText : ""
        variant: "small"
        color: Theme.fgMuted
        elide: Text.ElideRight
    }

    // The server stopped answering: say so instead of spinning silently.
    Rectangle {
        Layout.fillWidth: true
        visible: live.running && live.sync.live.stalled
        implicitHeight: stallColumn.implicitHeight + 2 * Theme.space3
        radius: Theme.radius
        color: Theme.warningBg
        Accessible.role: Accessible.AlertMessage
        Accessible.name: live.running ? live.sync.live.stallText : ""
        ColumnLayout {
            id: stallColumn
            anchors.fill: parent
            anchors.margins: Theme.space3
            spacing: Theme.space1
            BText {
                Layout.fillWidth: true
                text: live.running ? live.sync.live.stallText : ""
                wrapMode: Text.Wrap
            }
            BText {
                Layout.fillWidth: true
                visible: live.running && live.waitingFor().length > 0
                text: live.running ? qsTr("Waiting for: %1").arg(live.waitingFor().join(", ")) : ""
                variant: "small"
                color: Theme.fgMuted
                wrapMode: Text.Wrap
            }
            BButton {
                text: qsTr("Event log")
                iconName: "scroll-text"
                size: "small"
                variant: "ghost"
                onClicked: live.showLog()
            }
        }
    }
}
