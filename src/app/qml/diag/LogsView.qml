pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// LogsView — route "logs" ({group?, level?, search?, fromMs?, toMs?, run?}): the event log
// ("Rejestr zdarzeń") — what the app and especially the sync did, newest at the bottom. Filter
// by area (chips) and level, search (diacritics ignored), follow new entries while at the end
// ("Nowe wpisy (n)" otherwise). Copy (what is shown), share (Android), save (Downloads / Save
// as / browser download), clear; the detailed mode (one line per request to the server). The
// log stays on the device and never holds passwords or tokens (log/redact.h).
// Contract with the shell (qml/shell/ShellRouteView.qml): `route`, `title`.
FocusScope {
    id: root

    property var route: ({})
    readonly property string title: qsTr("Event log")
    readonly property bool hasWindow: (route.fromMs || 0) > 0 || (route.toMs || 0) > 0
    property bool windowCleared: false
    property bool following: true
    property int newCount: 0
    readonly property var groups: [
        { value: "", text: qsTr("Everything") },
        { value: "sync", text: qsTr("Sync") },
        { value: "net", text: qsTr("Network") },
        { value: "vault", text: qsTr("Notebook") },
        { value: "index", text: qsTr("Index") },
        { value: "reminders", text: qsTr("Reminders") },
        { value: "io", text: qsTr("Import and export") },
        { value: "app", text: qsTr("Application") },
        { value: "system", text: qsTr("System") }
    ]

    function groupText(group) {
        const g = groups.find(x => x.value === group)
        return g ? g.text : group
    }
    function levelText(level) {
        switch (level) {
        case "debug": return qsTr("Detail")
        case "info": return qsTr("Information")
        case "warning": return qsTr("Warning")
        case "error": return qsTr("Error")
        case "fatal": return qsTr("Fatal error")
        default: return level
        }
    }
    function toEnd() {
        list.positionViewAtEnd()
        root.newCount = 0
        root.following = true
    }
    function saveAs() {
        saveDialog.selectedFile = Logs.suggestedSaveFile()
        saveDialog.open()
    }
    function has(action) {
        return Logs.actions.indexOf(action) >= 0
    }

    focus: true

    LogModel {
        id: logModel
        group: root.route.group || ""
        minLevel: root.route.level || "debug"
        search: ""
        // Earlier runs of the app too (from the files): the sync that took long may have been
        // before the app was restarted.
        includeEarlier: true
        fromMs: root.windowCleared ? 0 : (root.route.fromMs || 0)
        toMs: root.windowCleared ? 0 : (root.route.toMs || 0)
        onCountChanged: {
            if (root.following)
                Qt.callLater(list.positionViewAtEnd)
        }
        onRowsInserted: (parent, first, last) => {
            if (!root.following)
                root.newCount += last - first + 1
        }
    }

    Connections {
        target: Logs
        function onDone(message) { AppController.toast(message, "success") }
        function onFailed(message) { AppController.toast(message, "error") }
    }

    TextMetrics {
        id: timeMetrics
        font.family: Theme.fontMono
        font.pixelSize: Theme.textXs
        text: "00:00:00.000"
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.bg
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- Search and actions ---------------------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.compact ? Theme.space3 : Theme.space6
            Layout.rightMargin: Theme.compact ? Theme.space3 : Theme.space6
            Layout.topMargin: Theme.compact ? Theme.space3 : Theme.space4
            spacing: Theme.space2

            BSearchField {
                id: searchField
                objectName: "logsSearch"
                Layout.fillWidth: true
                placeholderText: qsTr("Search the log…")
                onSearch: (query) => logModel.search = query
            }
            BButton {
                objectName: "logsCopy"
                visible: !Theme.compact
                text: qsTr("Copy")
                iconName: "copy"
                onClicked: Logs.copy(logModel)
            }
            BButton {
                objectName: "logsShare"
                visible: root.has("share") && !Theme.compact
                text: qsTr("Share")
                iconName: "share"
                variant: "primary"
                onClicked: Logs.share()
            }
            BButton {
                objectName: "logsSave"
                visible: !Theme.compact && (root.has("saveAs") || root.has("download")) && !root.has("share")
                text: root.has("download") ? qsTr("Download") : qsTr("Save as…")
                iconName: "download"
                variant: "primary"
                onClicked: root.has("download") ? Logs.download() : root.saveAs()
            }
            BIconButton {
                objectName: "logsShareCompact"
                visible: Theme.compact && (root.has("share") || root.has("download"))
                iconName: root.has("share") ? "share" : "download"
                label: root.has("share") ? qsTr("Share") : qsTr("Download")
                onClicked: root.has("share") ? Logs.share() : Logs.download()
            }
            BIconButton {
                id: moreButton
                objectName: "logsMore"
                iconName: "ellipsis-vertical"
                label: qsTr("More")
                onClicked: moreMenu.popup(moreButton, 0, moreButton.height)
            }
        }

        // ---- Filters -------------------------------------------------------------------------
        Flickable {
            id: chipStrip
            Layout.fillWidth: true
            Layout.topMargin: Theme.space2
            Layout.preferredHeight: chipRow.implicitHeight
            contentWidth: chipRow.implicitWidth + 2 * chipRow.x
            contentHeight: height
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Areas")
            Row {
                id: chipRow
                x: Theme.compact ? Theme.space3 : Theme.space6
                spacing: Theme.space2
                Repeater {
                    model: root.groups
                    delegate: BChip {
                        required property var modelData
                        objectName: "logsGroup-" + (modelData.value === "" ? "all" : modelData.value)
                        text: modelData.text
                        showIcon: false
                        interactive: true
                        selected: logModel.group === modelData.value
                        onClicked: logModel.group = modelData.value
                        Accessible.name: qsTr("Show: %1").arg(modelData.text)
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.compact ? Theme.space3 : Theme.space6
            Layout.rightMargin: Theme.compact ? Theme.space3 : Theme.space6
            Layout.topMargin: Theme.space2
            spacing: Theme.space2
            BSegmented {
                Layout.fillWidth: Theme.compact
                fill: Theme.compact
                size: "small"
                accessibleName: qsTr("Level")
                model: [{ value: "debug", text: qsTr("All") },
                        { value: "warning", text: Theme.compact ? qsTr("Warnings") : qsTr("Warnings and errors") },
                        { value: "error", text: qsTr("Errors") }]
                currentValue: logModel.minLevel === "info" ? "debug" : logModel.minLevel
                onActivated: (value) => logModel.minLevel = value
            }
            Item { Layout.fillWidth: true; visible: !Theme.compact }
            BText {
                visible: !Theme.compact
                text: logModel.filtered ? qsTr("%1 of %2 entries").arg(logModel.count).arg(logModel.totalCount)
                                        : qsTr("%n entries", "", logModel.count)
                variant: "small"
                color: Theme.fgMuted
            }
        }

        // ---- Banners: a sync run's time window, the detailed mode ---------------------------------
        Flow {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.compact ? Theme.space3 : Theme.space6
            Layout.rightMargin: Theme.compact ? Theme.space3 : Theme.space6
            Layout.topMargin: visible ? Theme.space2 : 0
            visible: (root.hasWindow && !root.windowCleared) || Logs.detailed
            spacing: Theme.space2
            BChip {
                visible: root.hasWindow && !root.windowCleared
                text: root.route.run ? qsTr("Sync #%1").arg(root.route.run) : qsTr("Time of one sync")
                iconName: "history"
                removable: true
                onRemoveClicked: root.windowCleared = true
            }
            BChip {
                visible: Logs.detailed
                variant: "reminder"
                text: qsTr("Detailed log is on")
                iconName: "info"
                removable: true
                Accessible.description: qsTr("Turn the detailed log off")
                onRemoveClicked: Logs.detailed = false
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.topMargin: Theme.space2
            implicitHeight: 1
            color: Theme.borderSubtle
        }

        // ---- The entries -----------------------------------------------------------------------
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: list
                objectName: "logsList"
                TouchFlick.enabled: true
                anchors.fill: parent
                clip: true
                model: logModel
                boundsBehavior: Flickable.StopAtBounds
                cacheBuffer: 400
                T.ScrollBar.vertical: BScrollBar {}
                Accessible.role: Accessible.List
                Accessible.name: root.title
                section.property: "day"
                section.delegate: Item {
                    id: dayHeader
                    required property string section
                    width: ListView.view.width
                    height: Theme.lineHeightPx(Theme.textSm) + Theme.space2 + Theme.space1
                    Rectangle { anchors.fill: parent; color: Theme.bgSunken }
                    Text {
                        x: Theme.space3
                        y: Theme.space1
                        height: Theme.lineHeightPx(Theme.textSm)
                        verticalAlignment: Text.AlignVCenter
                        text: DatesHelper.formatHuman(dayHeader.section)
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.textSm
                        font.weight: Theme.weightMedium
                        color: Theme.fgMuted
                        Accessible.role: Accessible.Heading
                        Accessible.name: text
                    }
                }
                delegate: LogRow {
                    levelText: root.levelText(level)
                    groupText: root.groupText(group)
                    timeWidth: timeMetrics.advanceWidth
                    required property string group
                    onCopyRequested: (index) => {
                        Logs.copyText(logModel.lineAt(index))
                        AppController.toast(qsTr("Copied the line."), "success")
                    }
                }
                header: Item {
                    width: list.width
                    height: earlierRow.visible ? earlierRow.implicitHeight + Theme.space2 : 0
                    BButton {
                        id: earlierRow
                        x: Theme.compact ? Theme.space3 : Theme.space6
                        y: Theme.space1
                        visible: Logs.hasEarlier
                        variant: "ghost"
                        size: "small"
                        iconName: logModel.includeEarlier ? "eye-off" : "history"
                        text: logModel.includeEarlier ? qsTr("Hide earlier runs of the app") : qsTr("Show earlier runs of the app")
                        onClicked: {
                            root.following = false
                            logModel.includeEarlier = !logModel.includeEarlier
                        }
                    }
                }
                footer: Item {
                    width: list.width
                    implicitHeight: footerColumn.implicitHeight + 2 * Theme.space4 + (Theme.narrow ? Theme.space16 : 0)
                    ColumnLayout {
                        id: footerColumn
                        x: Theme.compact ? Theme.space3 : Theme.space6
                        y: Theme.space4
                        width: Math.min(list.width - 2 * x, 760)
                        spacing: Theme.space2
                        BSwitch {
                            objectName: "logsDetailed"
                            Layout.fillWidth: true
                            text: qsTr("Detailed log")
                            description: qsTr("Also writes every request to the server (never note contents or passwords). Turn it on when syncing gives trouble.")
                            iconName: "scroll-text"
                            checked: Logs.detailed
                            onToggled: Logs.detailed = checked
                        }
                        BText {
                            Layout.fillWidth: true
                            text: qsTr("The log stays on this device; Brulion never sends it anywhere — only you share it. Passwords and tokens are removed; note names and the server address may appear.")
                            variant: "small"
                            color: Theme.fgMuted
                            wrapMode: Text.Wrap
                        }
                        BText {
                            Layout.fillWidth: true
                            visible: Logs.location !== ""
                            text: qsTr("Files: %1").arg(Logs.location)
                            variant: "small"
                            color: Theme.fgFaint
                            wrapMode: Text.WrapAnywhere
                        }
                    }
                }

                onMovementEnded: {
                    root.following = list.atYEnd
                    if (root.following)
                        root.newCount = 0
                }
                onFlickEnded: {
                    root.following = list.atYEnd
                    if (root.following)
                        root.newCount = 0
                }
                Component.onCompleted: Qt.callLater(list.positionViewAtEnd)
            }

            BEmptyState {
                anchors.centerIn: parent
                width: Math.min(parent.width - 2 * Theme.space6, implicitWidth)
                visible: logModel.count === 0
                iconName: "scroll-text"
                title: logModel.filtered ? qsTr("Nothing matches the filters") : qsTr("No entries")
                description: !Logs.available ? qsTr("The event log is not available in this mode.")
                           : logModel.filtered ? qsTr("Try another area or level, or clear the search.") : ""
            }

            // New entries arrived while reading older ones.
            BButton {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: Theme.narrow ? Theme.space16 + Theme.space4 : Theme.space4
                visible: root.newCount > 0 && !root.following
                text: qsTr("New entries (%n)", "", root.newCount)
                iconName: "arrow-down"
                variant: "primary"
                size: "small"
                onClicked: root.toEnd()
            }
        }
    }

    BMenu {
        id: moreMenu
        BMenuItem {
            visible: Theme.compact
            height: visible ? implicitHeight : 0
            text: qsTr("Copy")
            iconName: "copy"
            onTriggered: Logs.copy(logModel)
        }
        BMenuItem {
            visible: root.has("downloads")
            height: visible ? implicitHeight : 0
            text: qsTr("Save to Downloads")
            iconName: "download"
            onTriggered: Logs.saveToDownloads()
        }
        BMenuItem {
            visible: root.has("saveAs")
            height: visible ? implicitHeight : 0
            text: qsTr("Save as…")
            iconName: "file-output"
            onTriggered: root.saveAs()
        }
        BMenuItem {
            visible: root.has("openFolder")
            height: visible ? implicitHeight : 0
            text: qsTr("Open folder")
            iconName: "folder-open"
            onTriggered: Logs.openFolder()
        }
        BMenuItem {
            text: qsTr("Detailed log")
            checkable: true
            checked: Logs.detailed
            onTriggered: Logs.detailed = !Logs.detailed
        }
        BMenuItem {
            text: qsTr("Sync center")
            iconName: "folder-sync"
            onTriggered: AppController.navigate({ view: "sync", tab: "nextcloud" })
        }
        BMenuSeparator {}
        BMenuItem {
            text: qsTr("Clear the log…")
            iconName: "trash-2"
            danger: true
            onTriggered: clearConfirm.open()
        }
    }

    BConfirmDialog {
        id: clearConfirm
        title: qsTr("Clear the event log?")
        message: qsTr("All entries on this device are deleted, also those of earlier runs. Copy or save the log first if you want to send it.")
        confirmText: qsTr("Clear")
        destructive: true
        onAccepted: Logs.clear()
    }

    FileDialog {
        id: saveDialog
        objectName: "logsSaveDialog"
        title: qsTr("Save the event log")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Text files (*.txt)"), qsTr("All files (*)")]
        onAccepted: Logs.saveTo(selectedFile)
    }
}
