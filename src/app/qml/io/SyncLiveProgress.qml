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

    readonly property var sync: Workspace.sync
    readonly property var run: sync ? sync.live : ({})
    readonly property bool running: !!run && run.running === true

    signal showLog()

    visible: running
    spacing: Theme.space2

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.space2
        BSpinner {
            Layout.alignment: Qt.AlignVCenter
            running: live.running && !live.run.stalled
            visible: running
        }
        BIcon {
            Layout.alignment: Qt.AlignVCenter
            visible: live.running && live.run.stalled
            name: "triangle-alert"
            size: Theme.iconSize
            color: Theme.warningText
        }
        BText {
            Layout.fillWidth: true
            text: live.running ? live.run.phaseText : ""
            variant: "label"
            wrapMode: Text.Wrap
            Accessible.role: Accessible.StaticText
            Accessible.name: text + (live.running && live.run.detailText !== "" ? ", " + live.run.detailText : "")
        }
        BText {
            visible: text !== "" && !Theme.compact
            text: live.running ? live.run.detailText : ""
            variant: "small"
            color: Theme.fgMuted
        }
    }

    BText {
        Layout.fillWidth: true
        visible: Theme.compact && text !== ""
        text: live.running ? live.run.detailText : ""
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
            indeterminate: !live.running || live.run.progress < 0
            value: live.running && live.run.progress >= 0 ? live.run.progress : 0
            Accessible.name: qsTr("Sync progress")
        }
        // A long first sync over mobile data can be stopped: what is done stays synced.
        BButton {
            text: qsTr("Stop")
            iconName: "x"
            size: "small"
            enabled: live.running && !live.run.abandoning
            onClicked: live.sync.cancelSync()
        }
    }

    // Elapsed · bytes · trigger, e.g. "Trwa 1 min 20 s · 1,4 MB · po zmianie notatki".
    BText {
        Layout.fillWidth: true
        variant: "small"
        color: Theme.fgMuted
        wrapMode: Text.Wrap
        text: {
            if (!live.running)
                return ""
            const parts = [live.run.elapsedText]
            if (live.run.bytesText !== "")
                parts.push(live.run.bytesText)
            if (live.run.triggerText !== "")
                parts.push(live.run.triggerText)
            if (live.run.number > 0)
                parts.push("#" + live.run.number)
            return parts.join(" · ")
        }
    }

    BText {
        Layout.fillWidth: true
        visible: text !== ""
        text: live.running ? live.run.activeText : ""
        variant: "small"
        color: Theme.fgMuted
        elide: Text.ElideRight
    }

    // The server stopped answering: say so instead of spinning silently.
    Rectangle {
        Layout.fillWidth: true
        visible: live.running && live.run.stalled
        implicitHeight: stallColumn.implicitHeight + 2 * Theme.space3
        radius: Theme.radius
        color: Theme.warningBg
        Accessible.role: Accessible.AlertMessage
        Accessible.name: live.running ? live.run.stallText : ""
        ColumnLayout {
            id: stallColumn
            anchors.fill: parent
            anchors.margins: Theme.space3
            spacing: Theme.space1
            BText {
                Layout.fillWidth: true
                text: live.running ? live.run.stallText : ""
                wrapMode: Text.Wrap
            }
            BText {
                Layout.fillWidth: true
                visible: live.running && live.run.waitingFor.length > 0
                text: live.running ? qsTr("Waiting for: %1").arg(live.run.waitingFor.join(", ")) : ""
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
