pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// SyncStatusPanel — the Nextcloud sync state (Workspace.sync): what is happening now, progress,
// offline and error explanations, "Synchronizuj teraz", when it last / next runs and what the last
// run did (sent, downloaded, merged, conflicts, errors — failed files listed).
//
//   SyncStatusPanel { onShowConflicts: tabs.currentIndex = 3 }
IoCard {
    id: panel

    readonly property var sync: Workspace.sync
    readonly property string syncState: sync ? sync.state : "disabled"
    readonly property bool syncing: syncState === "syncing"
    readonly property var report: sync ? sync.lastReport : ({})
    readonly property bool hasReport: !!report && report.finished !== undefined && report.finished !== ""
    property bool showErrors: false

    signal showConflicts()

    function nextText() {
        if (!sync || sync.nextSync === "")
            return ""
        return DatesHelper.formatHuman(sync.nextSync.replace("T", " ").slice(0, 16))
    }

    iconName: syncState === "syncing" ? "refresh-cw"
            : syncState === "offline" ? "cloud-off"
            : syncState === "error" ? "circle-alert"
            : syncState === "idle" ? "circle-check" : "cloud"
    iconColor: syncState === "error" ? (needsUser ? Theme.dangerText : Theme.warningText)
             : syncState === "offline" ? Theme.warningText
             : syncState === "idle" ? Theme.successText : Theme.fgMuted
    // Problems that need the user (password, folder, storage) are red; passing ones (network)
    // only amber — Brulion retries by itself.
    readonly property bool needsUser: syncState === "error" && ["network", "timeout", "server"].indexOf(sync ? sync.lastErrorCode : "") < 0
    tone: syncState === "error" ? (needsUser ? "danger" : "warning") : "default"
    title: qsTr("Nextcloud")
    subtitle: !sync ? ""
            : sync.statusText !== "" ? sync.statusText
            : qsTr("Not set up. Fill in the form below to start.")

    trailing: [
        BButton {
            visible: panel.sync && panel.sync.active && !Theme.compact
            text: qsTr("Sync now")
            iconName: "refresh-cw"
            variant: "primary"
            busy: panel.syncing
            onClicked: panel.sync.syncNow()
        }
    ]

    // ---- Now ---------------------------------------------------------------------------------
    BProgressBar {
        Layout.fillWidth: true
        visible: panel.syncing
        indeterminate: !panel.sync || panel.sync.progress < 0
        value: panel.sync && panel.sync.progress >= 0 ? panel.sync.progress : 0
    }

    Rectangle {
        Layout.fillWidth: true
        visible: panel.syncState === "offline"
        implicitHeight: offlineRow.implicitHeight + 2 * Theme.space3
        radius: Theme.radius
        color: Theme.warningBg
        RowLayout {
            id: offlineRow
            anchors.fill: parent
            anchors.margins: Theme.space3
            spacing: Theme.space2
            BIcon { Layout.alignment: Qt.AlignTop; name: "cloud-off"; size: Theme.iconSize; color: Theme.warningText }
            BText {
                Layout.fillWidth: true
                //: Polish: "Pracujesz offline — zmiany zsynchronizują się później."
                text: qsTr("You are working offline — changes will sync later.")
                wrapMode: Text.Wrap
            }
        }
    }

    BText {
        Layout.fillWidth: true
        visible: panel.syncState === "error" && text !== ""
        variant: "muted"
        wrapMode: Text.Wrap
        text: {
            switch (panel.sync ? panel.sync.lastErrorCode : "") {
            case "auth": return qsTr("Enter a new app password below and save. Brulion keeps trying with the old one until then.")
            case "network":
            case "timeout": return qsTr("Brulion tries again by itself; your notes stay safe on this device.")
            case "not-found": return qsTr("Check the server folder below – or let the first sync create it.")
            case "local-empty":
            case "remote-empty": return qsTr("Nothing was deleted. Check that the notebook folder and the server folder are the right ones, then sync again.")
            case "vault-unavailable": return qsTr("Insert the card or connect the drive with the notebook, then sync again.")
            case "insufficient-storage": return qsTr("Free some space on the server; nothing is lost meanwhile.")
            default: return ""
            }
        }
    }

    // ---- When ------------------------------------------------------------------------------
    BText {
        Layout.fillWidth: true
        visible: text !== ""
        variant: "small"
        color: Theme.fgMuted
        wrapMode: Text.Wrap
        text: {
            if (!panel.sync || !panel.sync.active)
                return ""
            const parts = []
            if (panel.sync.lastSyncText !== "")
                parts.push(qsTr("Last synced %1").arg(panel.sync.lastSyncText))
            const next = panel.nextText()
            if (next !== "" && !panel.syncing)
                parts.push(panel.syncState === "error" || panel.syncState === "offline" ? qsTr("next attempt %1").arg(next)
                                                                                         : qsTr("next sync %1").arg(next))
            return parts.join(" · ")
        }
    }

    // ---- What the last run did ------------------------------------------------------------------
    GridLayout {
        Layout.fillWidth: true
        visible: panel.hasReport
        columns: Theme.compact ? 3 : 5
        columnSpacing: Theme.space2
        rowSpacing: Theme.space2

        Repeater {
            model: panel.hasReport ? [
                { label: qsTr("Sent"), value: panel.report.uploaded, icon: "upload", tone: "" },
                { label: qsTr("Downloaded"), value: panel.report.downloaded, icon: "download", tone: "" },
                { label: qsTr("Merged"), value: panel.report.merged, icon: "git-merge", tone: "" },
                { label: qsTr("Conflicts"), value: panel.report.conflicts, icon: "git-compare", tone: panel.report.conflicts > 0 ? "warning" : "" },
                { label: qsTr("Errors"), value: (panel.report.errors || []).length, icon: "circle-alert", tone: (panel.report.errors || []).length > 0 ? "danger" : "" }
            ] : []
            delegate: Rectangle {
                id: tile
                required property var modelData
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                implicitHeight: tileColumn.implicitHeight + 2 * Theme.space3
                radius: Theme.radius
                color: modelData.tone === "warning" ? Theme.warningBg : modelData.tone === "danger" ? Theme.dangerBg : Theme.bgSunken
                Accessible.role: Accessible.StaticText
                Accessible.name: modelData.label + ": " + modelData.value
                ColumnLayout {
                    id: tileColumn
                    anchors.fill: parent
                    anchors.margins: Theme.space3
                    spacing: 2
                    RowLayout {
                        spacing: Theme.space1
                        BIcon { name: tile.modelData.icon; size: Theme.iconSizeSm; color: Theme.fgMuted }
                        Text {
                            text: tile.modelData.value
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.textXl
                            font.weight: Theme.weightSemiBold
                            color: tile.modelData.tone === "warning" ? Theme.warningText
                                 : tile.modelData.tone === "danger" ? Theme.dangerText : Theme.fg
                        }
                    }
                    BText {
                        Layout.fillWidth: true
                        text: tile.modelData.label
                        variant: "small"
                        color: Theme.fgMuted
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }

    BText {
        Layout.fillWidth: true
        visible: panel.hasReport && (panel.report.deletedLocal > 0 || panel.report.deletedRemote > 0)
        variant: "small"
        color: Theme.fgMuted
        wrapMode: Text.Wrap
        text: panel.hasReport ? qsTr("Deleted: %1 on this device, %2 on the server (they are in the trash or the server's deleted files).")
                                .arg(panel.report.deletedLocal).arg(panel.report.deletedRemote) : ""
    }

    RowLayout {
        Layout.fillWidth: true
        visible: panel.hasReport && panel.report.conflicts > 0
        spacing: Theme.space2
        BText {
            Layout.fillWidth: true
            text: qsTr("Both versions of the conflicting notes were kept – merge them in Conflicts.")
            variant: "muted"
            wrapMode: Text.Wrap
        }
        BButton {
            text: qsTr("Show")
            size: "small"
            onClicked: panel.showConflicts()
        }
    }

    // Files that failed (usually few; at most 20 shown).
    BButton {
        visible: panel.hasReport && (panel.report.errors || []).length > 0
        variant: "ghost"
        size: "small"
        iconName: panel.showErrors ? "chevron-down" : "chevron-right"
        text: qsTr("%n file(s) could not be synced", "", panel.hasReport ? (panel.report.errors || []).length : 0)
        onClicked: panel.showErrors = !panel.showErrors
    }
    Repeater {
        model: panel.showErrors && panel.hasReport ? (panel.report.errors || []).slice(0, 20) : []
        delegate: ColumnLayout {
            id: errorRow
            required property var modelData
            Layout.fillWidth: true
            Layout.leftMargin: Theme.space4
            spacing: 0
            BText {
                Layout.fillWidth: true
                text: errorRow.modelData.path
                variant: "label"
                elide: Text.ElideMiddle
            }
            BText {
                Layout.fillWidth: true
                text: errorRow.modelData.message
                variant: "small"
                color: Theme.fgMuted
                wrapMode: Text.Wrap
            }
        }
    }

    // Phones: the main action at the end of the card, full width.
    BButton {
        Layout.fillWidth: true
        visible: !!panel.sync && panel.sync.active && Theme.compact
        text: qsTr("Sync now")
        iconName: "refresh-cw"
        variant: "primary"
        busy: panel.syncing
        onClicked: panel.sync.syncNow()
    }
}
