pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// SyncRunDetails — "Szczegóły" of one sync run (Workspace.sync.runDetails(number)): the result
// with its explanation and code, how long each phase took (a bar and a list for screen
// readers), what was sent, downloaded, merged and deleted, the requests to the server (count,
// average, the slowest), files that failed and conflicts. "Pokaż w rejestrze" opens the event
// log at the run's time; "Kopiuj szczegóły" copies all of it as text.
//
//   SyncRunDetails { number: 42; onClosed: … }   then open()
BDialog {
    id: dialog

    property int number: 0
    readonly property var details: Workspace.sync && number > 0 ? Workspace.sync.runDetails(number) : ({})
    readonly property bool known: details.number !== undefined
    readonly property var phases: known ? details.phases : []
    readonly property var stats: known ? details.stats : ({})
    readonly property var counts: known ? details.counts : ({})

    function toneColor(tone) {
        switch (tone) {
        case "success": return Theme.successText
        case "warning": return Theme.warningText
        case "danger": return Theme.dangerText
        default: return Theme.fgMuted
        }
    }
    function phaseColor(index) {
        const colors = [Theme.info, Theme.success, Theme.warning, Theme.chipScheduledFg, Theme.marginRed, Theme.fgFaint]
        return colors[index % colors.length]
    }

    title: qsTr("Sync #%1").arg(number)
    subtitle: known ? [details.startedText, details.triggerText, details.durationText].filter(s => s !== "").join(" · ") : ""
    iconName: "history"
    acceptText: qsTr("Close")
    showReject: false
    preferredWidth: Math.min(640, Theme.windowWidth - 2 * Theme.space6)
    onAccepted: close()

    footerLeading: [
        BButton {
            objectName: "syncDetailsShowLog"
            text: Theme.compact ? qsTr("Log") : qsTr("Show in the event log")
            iconName: "scroll-text"
            variant: "ghost"
            visible: dialog.known
            onClicked: {
                dialog.close()
                AppController.navigate({ view: "logs", fromMs: dialog.details.fromMs, toMs: dialog.details.toMs,
                                         run: dialog.number })
            }
        },
        BButton {
            text: Theme.compact ? qsTr("Copy") : qsTr("Copy details")
            iconName: "copy"
            variant: "ghost"
            visible: dialog.known
            onClicked: {
                Logs.copyText(Workspace.sync.runDetailsText(dialog.number))
                AppController.toast(qsTr("Copied the details of sync #%1.").arg(dialog.number), "success")
            }
        }
    ]

    BText {
        Layout.fillWidth: true
        visible: !dialog.known
        text: qsTr("This run is no longer in the history.")
        variant: "muted"
        wrapMode: Text.Wrap
    }

    // ---- Result ------------------------------------------------------------------------------
    RowLayout {
        Layout.fillWidth: true
        visible: dialog.known
        spacing: Theme.space2
        Rectangle {
            Layout.alignment: Qt.AlignVCenter
            implicitWidth: 10
            implicitHeight: 10
            radius: 5
            color: dialog.toneColor(dialog.details.tone)
        }
        BText {
            Layout.fillWidth: true
            text: dialog.known ? dialog.details.outcomeText : ""
            variant: "label"
            color: dialog.toneColor(dialog.details.tone)
            wrapMode: Text.Wrap
        }
    }
    BText {
        Layout.fillWidth: true
        visible: dialog.known && dialog.details.errorText !== ""
        text: dialog.known ? dialog.details.errorText : ""
        wrapMode: Text.Wrap
    }
    BText {
        Layout.fillWidth: true
        visible: dialog.known && (dialog.details.errorCode !== "" || dialog.details.errorMessage !== "")
        text: dialog.known ? [dialog.details.errorCode !== "" ? qsTr("Code: %1").arg(dialog.details.errorCode) : "",
                              dialog.details.errorMessage].filter(s => s !== "").join(" · ") : ""
        variant: "small"
        color: Theme.fgMuted
        wrapMode: Text.WrapAnywhere
    }

    // ---- Phases ------------------------------------------------------------------------------
    BText {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space2
        visible: dialog.phases.length > 0
        text: qsTr("Where the time went")
        variant: "label"
        Accessible.role: Accessible.Heading
    }
    Row {
        id: phaseBar
        Layout.fillWidth: true
        Layout.preferredHeight: 10
        visible: dialog.phases.length > 0
        spacing: 1
        Accessible.ignored: true
        Repeater {
            model: dialog.phases
            delegate: Rectangle {
                required property var modelData
                required property int index
                width: Math.max(2, (phaseBar.width - dialog.phases.length) * modelData.share)
                height: phaseBar.height
                radius: 2
                color: dialog.phaseColor(index)
            }
        }
    }
    Repeater {
        model: dialog.phases
        delegate: RowLayout {
            id: phaseRow
            required property var modelData
            required property int index
            Layout.fillWidth: true
            spacing: Theme.space2
            Accessible.role: Accessible.StaticText
            Accessible.name: modelData.text + ": " + modelData.durationText + (modelData.detailText !== "" ? ", " + modelData.detailText : "")
            Rectangle {
                Layout.alignment: Qt.AlignVCenter
                implicitWidth: 8
                implicitHeight: 8
                radius: 2
                color: dialog.phaseColor(phaseRow.index)
            }
            BText {
                Layout.fillWidth: true
                text: phaseRow.modelData.text + (phaseRow.modelData.detailText !== "" ? " · " + phaseRow.modelData.detailText : "")
                variant: "small"
                wrapMode: Text.Wrap
            }
            BText {
                text: phaseRow.modelData.durationText
                variant: "small"
                color: Theme.fgMuted
            }
        }
    }

    // ---- What changed --------------------------------------------------------------------------
    BText {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space2
        visible: dialog.known
        text: qsTr("Changes")
        variant: "label"
        Accessible.role: Accessible.Heading
    }
    BText {
        Layout.fillWidth: true
        visible: dialog.known
        wrapMode: Text.Wrap
        variant: "small"
        text: dialog.known
              ? qsTr("Sent %1 · downloaded %2 · merged %3 · deleted %4 here and %5 on the server · conflicts %6 · errors %7")
                .arg(dialog.counts.uploaded).arg(dialog.counts.downloaded).arg(dialog.counts.merged)
                .arg(dialog.counts.deletedLocal).arg(dialog.counts.deletedRemote).arg(dialog.counts.conflicts)
                .arg(dialog.counts.fileErrors)
              : ""
    }
    BText {
        readonly property string folders: dialog.known && Workspace.sync
            ? Workspace.sync.folderCountsText(dialog.counts.foldersCreated || 0, dialog.counts.foldersDeleted || 0,
                                              dialog.counts.foldersMoved || 0, dialog.counts.foldersMovedHere || 0)
            : ""
        Layout.fillWidth: true
        visible: folders !== ""
        wrapMode: Text.Wrap
        variant: "small"
        text: folders
    }
    BText {
        readonly property string files: dialog.known && Workspace.sync
            ? Workspace.sync.fileMovesText(dialog.counts.filesMoved || 0, dialog.counts.filesMovedHere || 0)
            : ""
        Layout.fillWidth: true
        visible: files !== ""
        wrapMode: Text.Wrap
        variant: "small"
        text: files
    }

    // ---- Requests ------------------------------------------------------------------------------
    BText {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space2
        visible: dialog.known && dialog.stats.requests > 0
        text: qsTr("Requests to the server")
        variant: "label"
        Accessible.role: Accessible.Heading
    }
    BText {
        Layout.fillWidth: true
        visible: dialog.known && dialog.stats.requests > 0
        variant: "small"
        wrapMode: Text.Wrap
        text: dialog.known
              ? [dialog.stats.requestsText,
                 dialog.stats.averageText !== "" ? qsTr("average %1").arg(dialog.stats.averageText) : "",
                 dialog.stats.bytesText, dialog.stats.protocol,
                 dialog.stats.timeouts > 0 ? qsTr("%n timeout(s)", "", dialog.stats.timeouts) : "",
                 dialog.stats.stallText !== "" ? qsTr("longest wait %1").arg(dialog.stats.stallText) : ""]
                .filter(s => s !== "").join(" · ")
              : ""
    }
    Repeater {
        model: dialog.known ? dialog.stats.slowest : []
        delegate: BText {
            required property var modelData
            Layout.fillWidth: true
            Layout.leftMargin: Theme.space3
            text: modelData.method + " " + modelData.title + " → " + (modelData.status > 0 ? modelData.status : "—") + " · " + modelData.msText
            variant: "small"
            color: Theme.fgMuted
            elide: Text.ElideMiddle
        }
    }

    // ---- Files that failed -----------------------------------------------------------------------
    BText {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space2
        visible: dialog.known && dialog.details.errors.length > 0
        text: qsTr("Files that could not be synced")
        variant: "label"
        Accessible.role: Accessible.Heading
    }
    Repeater {
        model: dialog.known ? dialog.details.errors : []
        delegate: ColumnLayout {
            id: errorRow
            required property var modelData
            Layout.fillWidth: true
            spacing: 0
            BText {
                Layout.fillWidth: true
                text: errorRow.modelData.path
                variant: "label"
                elide: Text.ElideMiddle
            }
            BText {
                Layout.fillWidth: true
                text: errorRow.modelData.text
                wrapMode: Text.Wrap
            }
            BText {
                Layout.fillWidth: true
                text: errorRow.modelData.code + " · " + errorRow.modelData.message
                variant: "small"
                color: Theme.fgMuted
                wrapMode: Text.WrapAnywhere
            }
        }
    }
    BText {
        Layout.fillWidth: true
        visible: dialog.known && dialog.details.moreErrors > 0
        text: qsTr("…and %n more", "", dialog.known ? dialog.details.moreErrors : 0)
        variant: "small"
        color: Theme.fgMuted
    }

    // ---- Conflicts ---------------------------------------------------------------------------
    BText {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space2
        visible: dialog.known && dialog.details.conflicts.length > 0
        text: qsTr("Conflicts (both versions kept)")
        variant: "label"
        Accessible.role: Accessible.Heading
    }
    Repeater {
        model: dialog.known ? dialog.details.conflicts : []
        delegate: BText {
            required property var modelData
            Layout.fillWidth: true
            text: modelData.path
            variant: "small"
            elide: Text.ElideMiddle
        }
    }
    BButton {
        visible: dialog.known && dialog.details.conflicts.length > 0
        text: qsTr("Merge")
        iconName: "git-merge"
        size: "small"
        onClicked: {
            dialog.close()
            AppController.navigate({ view: "sync", tab: "conflicts" })
        }
    }
}
