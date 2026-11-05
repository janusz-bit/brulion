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
    readonly property alias run: runInfo
    readonly property bool known: runInfo.known

    SyncRunInfo {
        id: runInfo
        sync: Workspace.sync
        number: dialog.number
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
    function toneColor(tone: string): color {
        switch (tone) {
        case "success": return Theme.successText
        case "warning": return Theme.warningText
        case "danger": return Theme.dangerText
        default: return Theme.fgMuted
        }
    }
    function phaseColor(index: int): color {
        switch (index % 6) {
        case 0: return Theme.info
        case 1: return Theme.success
        case 2: return Theme.warning
        case 3: return Theme.chipScheduledFg
        case 4: return Theme.marginRed
        default: return Theme.fgFaint
        }
    }

    title: qsTr("Sync #%1").arg(number)
    subtitle: known ? joinNonEmpty([runInfo.details.startedText, runInfo.details.triggerText, runInfo.details.durationText]) : ""
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
                AppController.navigate({ view: "logs", fromMs: runInfo.details.fromMs, toMs: runInfo.details.toMs,
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
            color: dialog.toneColor(dialog.known ? runInfo.details.tone : "")
        }
        BText {
            Layout.fillWidth: true
            text: dialog.known ? runInfo.details.outcomeText : ""
            variant: "label"
            color: dialog.toneColor(dialog.known ? runInfo.details.tone : "")
            wrapMode: Text.Wrap
        }
    }
    BText {
        Layout.fillWidth: true
        visible: dialog.known && runInfo.details.errorText !== ""
        text: dialog.known ? runInfo.details.errorText : ""
        wrapMode: Text.Wrap
    }
    BText {
        Layout.fillWidth: true
        visible: dialog.known && (runInfo.details.errorCode !== "" || runInfo.details.errorMessage !== "")
        text: dialog.known ? dialog.joinNonEmpty([runInfo.details.errorCode !== "" ? qsTr("Code: %1").arg(runInfo.details.errorCode) : "",
                                                  runInfo.details.errorMessage]) : ""
        variant: "small"
        color: Theme.fgMuted
        wrapMode: Text.WrapAnywhere
    }

    // ---- Phases ------------------------------------------------------------------------------
    BText {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space2
        visible: runInfo.phases.length > 0
        text: qsTr("Where the time went")
        variant: "label"
        Accessible.role: Accessible.Heading
    }
    Row {
        id: phaseBar
        Layout.fillWidth: true
        Layout.preferredHeight: 10
        visible: runInfo.phases.length > 0
        spacing: 1
        Accessible.ignored: true
        Repeater {
            model: runInfo.phases
            delegate: Rectangle {
                required property real share
                required property int index
                width: Math.max(2, (phaseBar.width - runInfo.phases.length) * share)
                height: phaseBar.height
                radius: 2
                color: dialog.phaseColor(index)
            }
        }
    }
    Repeater {
        model: runInfo.phases
        delegate: RowLayout {
            id: phaseRow
            required property string text
            required property string durationText
            required property string detailText
            required property int index
            Layout.fillWidth: true
            spacing: Theme.space2
            Accessible.role: Accessible.StaticText
            Accessible.name: text + ": " + durationText + (detailText !== "" ? ", " + detailText : "")
            Rectangle {
                Layout.alignment: Qt.AlignVCenter
                implicitWidth: 8
                implicitHeight: 8
                radius: 2
                color: dialog.phaseColor(phaseRow.index)
            }
            BText {
                Layout.fillWidth: true
                text: phaseRow.text + (phaseRow.detailText !== "" ? " · " + phaseRow.detailText : "")
                variant: "small"
                wrapMode: Text.Wrap
            }
            BText {
                text: phaseRow.durationText
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
                .arg("" + runInfo.counts.uploaded).arg("" + runInfo.counts.downloaded).arg("" + runInfo.counts.merged)
                .arg("" + runInfo.counts.deletedLocal).arg("" + runInfo.counts.deletedRemote).arg("" + runInfo.counts.conflicts)
                .arg("" + runInfo.counts.fileErrors)
              : ""
    }
    BText {
        readonly property string folders: dialog.known && Workspace.sync
            ? Workspace.sync.folderCountsText(runInfo.counts.foldersCreated || 0, runInfo.counts.foldersDeleted || 0,
                                              runInfo.counts.foldersMoved || 0, runInfo.counts.foldersMovedHere || 0)
            : ""
        Layout.fillWidth: true
        visible: folders !== ""
        wrapMode: Text.Wrap
        variant: "small"
        text: folders
    }
    BText {
        readonly property string files: dialog.known && Workspace.sync
            ? Workspace.sync.fileMovesText(runInfo.counts.filesMoved || 0, runInfo.counts.filesMovedHere || 0)
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
        visible: dialog.known && runInfo.stats.requests > 0
        text: qsTr("Requests to the server")
        variant: "label"
        Accessible.role: Accessible.Heading
    }
    BText {
        Layout.fillWidth: true
        visible: dialog.known && runInfo.stats.requests > 0
        variant: "small"
        wrapMode: Text.Wrap
        text: dialog.known
              ? dialog.joinNonEmpty([runInfo.stats.requestsText,
                                     runInfo.stats.averageText !== "" ? qsTr("average %1").arg(runInfo.stats.averageText) : "",
                                     runInfo.stats.bytesText, runInfo.stats.protocol,
                                     runInfo.stats.timeouts > 0 ? qsTr("%n timeout(s)", "", runInfo.stats.timeouts) : "",
                                     runInfo.stats.stallText !== "" ? qsTr("longest wait %1").arg(runInfo.stats.stallText) : ""])
              : ""
    }
    Repeater {
        model: runInfo.slowest
        delegate: BText {
            required property string method
            required property string title
            required property int status
            required property string msText
            Layout.fillWidth: true
            Layout.leftMargin: Theme.space3
            text: method + " " + title + " → " + (status > 0 ? "" + status : "—") + " · " + msText
            variant: "small"
            color: Theme.fgMuted
            elide: Text.ElideMiddle
        }
    }

    // ---- Files that failed -----------------------------------------------------------------------
    BText {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space2
        visible: runInfo.errors.length > 0
        text: qsTr("Files that could not be synced")
        variant: "label"
        Accessible.role: Accessible.Heading
    }
    Repeater {
        model: runInfo.errors
        delegate: ColumnLayout {
            id: errorRow
            required property string path
            required property string text
            required property string code
            required property string message
            Layout.fillWidth: true
            spacing: 0
            BText {
                Layout.fillWidth: true
                text: errorRow.path
                variant: "label"
                elide: Text.ElideMiddle
            }
            BText {
                Layout.fillWidth: true
                text: errorRow.text
                wrapMode: Text.Wrap
            }
            BText {
                Layout.fillWidth: true
                text: errorRow.code + " · " + errorRow.message
                variant: "small"
                color: Theme.fgMuted
                wrapMode: Text.WrapAnywhere
            }
        }
    }
    BText {
        Layout.fillWidth: true
        visible: dialog.known && runInfo.details.moreErrors > 0
        text: qsTr("…and %n more", "", dialog.known ? runInfo.details.moreErrors : 0)
        variant: "small"
        color: Theme.fgMuted
    }

    // ---- Conflicts ---------------------------------------------------------------------------
    BText {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space2
        visible: runInfo.conflicts.length > 0
        text: qsTr("Conflicts (both versions kept)")
        variant: "label"
        Accessible.role: Accessible.Heading
    }
    Repeater {
        model: runInfo.conflicts
        delegate: BText {
            required property string path
            Layout.fillWidth: true
            text: path
            variant: "small"
            elide: Text.ElideMiddle
        }
    }
    BButton {
        visible: runInfo.conflicts.length > 0
        text: qsTr("Merge")
        iconName: "git-merge"
        size: "small"
        onClicked: {
            dialog.close()
            AppController.navigate({ view: "sync", tab: "conflicts" })
        }
    }
}
