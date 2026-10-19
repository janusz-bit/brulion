pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// SyncHistoryList — "Historia synchronizacji": the last Nextcloud sync runs (Workspace.sync.
// history, newest first; quiet runs folded into one row "Bez zmian ×12"), each with its result,
// when and why it ran, what it moved and how long it took. A row opens SyncRunDetails.
//
//   SyncHistoryList { Layout.fillWidth: true }
IoCard {
    id: card

    readonly property var sync: Workspace.sync
    readonly property var runs: sync ? sync.history : []
    property bool showAll: false
    readonly property int shown: showAll ? runs.length : Math.min(runs.length, 5)

    function iconFor(run) {
        switch (run.outcome) {
        case "ok": return "circle-check"
        case "conflicts": return "git-merge"
        case "files": return "triangle-alert"
        case "needs-confirmation": return "hand"
        case "cancelled": return "circle-x"
        default: return "circle-alert"
        }
    }
    function colorFor(tone) {
        switch (tone) {
        case "success": return Theme.successText
        case "warning": return Theme.warningText
        case "danger": return Theme.dangerText
        default: return Theme.fgMuted
        }
    }
    function open(number) {
        detailsLoader.number = number
        detailsLoader.active = true
    }

    visible: runs.length > 0
    iconName: "history"
    title: qsTr("Sync history")
    subtitle: qsTr("The last %n run(s) on this device.", "", runs.length)
    contentSpacing: Theme.space1

    trailing: [
        BIconButton {
            id: moreButton
            iconName: "ellipsis"
            label: qsTr("More")
            onClicked: historyMenu.popup(moreButton, 0, moreButton.height)
        }
    ]

    BMenu {
        id: historyMenu
        BMenuItem {
            text: qsTr("Event log")
            iconName: "scroll-text"
            onTriggered: AppController.navigate({ view: "logs", group: "sync" })
        }
        BMenuItem {
            text: qsTr("Clear history")
            iconName: "trash-2"
            danger: true
            onTriggered: card.sync.clearHistory()
        }
    }

    Repeater {
        model: card.runs.slice(0, card.shown)
        delegate: BListItem {
            id: row
            required property var modelData
            objectName: "syncHistoryRow"
            Layout.fillWidth: true
            iconName: card.iconFor(modelData)
            iconColor: card.colorFor(modelData.tone)
            text: modelData.outcomeText
            subtitle: [modelData.startedText, modelData.triggerText, modelData.summary].filter(s => s !== "").join(" · ")
            trailing: BIcon { name: "chevron-right"; size: Theme.iconSizeSm; color: Theme.fgFaint }
            onClicked: card.open(row.modelData.number)
            Accessible.description: qsTr("Opens the details of this sync")
        }
    }

    BButton {
        visible: card.runs.length > 5
        variant: "ghost"
        size: "small"
        iconName: card.showAll ? "chevron-up" : "chevron-down"
        text: card.showAll ? qsTr("Show fewer") : qsTr("Show all (%1)").arg(card.runs.length)
        onClicked: card.showAll = !card.showAll
    }

    Loader {
        id: detailsLoader
        property int number: 0
        active: false
        sourceComponent: SyncRunDetails {
            objectName: "syncRunDetails"
            number: detailsLoader.number
            onClosed: detailsLoader.active = false
        }
        onLoaded: (detailsLoader.item as SyncRunDetails).open()
    }
}
