pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// CalendarsPanel — the sync center's Calendars tab (Workspace.calendars; docs/UI.md "Calendars"):
//   status       what the calendars did last ("Calendars updated today 10:32", a failure, offline),
//                "Refresh all"; deletions of synced tasks held back by the guard (their titles,
//                "Delete them"); in the browser a word on CORS
//   subscriptions calendars by address (ICS, webcal://), read-only: each with its colour (a menu
//                of colours), name, when it was fetched or why not, how often it is fetched again,
//                "Refresh", "Remove"; a field to subscribe to another one
//   accounts     each CalDAV account (user @ server): when both sides changed a task, keep the
//                newer or both; how often it syncs; its calendars — events in the calendar, tasks
//                in the notes and the page they land on; "Add a CalDAV account…"
//                (CalendarAccountDialog), "Remove the account…"
//   log          what the runs did: updates, synced counts, conflicts and how they were decided,
//                deletions waiting, errors; "Clear"
ColumnLayout {
    id: panel

    readonly property WorkspaceCalendars calendars: Workspace.calendars
    readonly property list<var> noRows: []
    readonly property list<string> noColors: []
    readonly property list<var> subscriptionRows: calendars !== null ? calendars.subscriptions : noRows
    readonly property list<var> accountRows: calendars !== null ? calendars.accounts : noRows
    readonly property list<var> logRows: calendars !== null ? calendars.log : noRows
    readonly property string calState: calendars !== null ? calendars.state : "disabled"
    property string addError
    property bool showAllLog: false
    /// The row whose colour menu is open, and the account being removed.
    property string colorFor
    property string removing
    property string removingName

    Layout.fillWidth: true
    spacing: Theme.space5

    /// The non-empty parts joined with " · ".
    function joinDots(parts: list<string>): string {
        return Js.joinNonEmpty(parts, " · ")
    }

    Connections {
        target: panel.calendars
        function onErrorOccurred(message: string): void {
            panel.addError = message
        }
    }

    component Hint: BText {
        Layout.fillWidth: true
        variant: "small"
        color: Theme.fgMuted
        wrapMode: Text.Wrap
    }

    /// One subscription or calendar: colour, name, status, refresh, more.
    component SourceRow: ColumnLayout {
        id: source
        required property var modelData
        readonly property string sourceId: AppController.routeText(modelData, "id")
        readonly property string kind: AppController.routeText(modelData, "kind")
        readonly property string error: AppController.routeText(modelData, "error")
        Layout.fillWidth: true
        spacing: Theme.space1
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space2
            BColorSwatch {
                id: swatch
                objectName: "calendarColor"
                Layout.alignment: Qt.AlignVCenter
                size: Theme.touchUi ? 28 : 20
                readonly property string colorText: AppController.routeText(source.modelData, "color")
                color: colorText
                label: qsTr("Colour of “%1”").arg(AppController.routeText(source.modelData, "name"))
                onClicked: {
                    panel.colorFor = source.sourceId
                    colorMenu.popup(swatch, 0, swatch.height)
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                BText {
                    Layout.fillWidth: true
                    text: AppController.routeText(source.modelData, "name")
                    font.weight: Theme.weightSemiBold
                    elide: Text.ElideRight
                }
                BText {
                    Layout.fillWidth: true
                    text: source.error !== "" ? AppController.routeText(source.modelData, "errorText")
                                              : AppController.routeText(source.modelData, "statusText")
                    variant: "small"
                    color: source.error !== "" ? Theme.dangerText : Theme.fgMuted
                    wrapMode: Text.Wrap
                }
            }
            BIconButton {
                iconName: "refresh-cw"
                label: qsTr("Refresh now")
                enabled: panel.calendars !== null && !panel.calendars.running
                onClicked: panel.calendars.refresh(source.sourceId)
            }
            BIconButton {
                iconName: AppController.routeFlag(source.modelData, "visible") ? "eye" : "eye-off"
                label: AppController.routeFlag(source.modelData, "visible") ? qsTr("Shown in the calendar") : qsTr("Hidden in the calendar")
                onClicked: panel.calendars.setCalendarVisible(source.sourceId, !AppController.routeFlag(source.modelData, "visible"))
            }
            BIconButton {
                iconName: "trash-2"
                label: source.kind === "subscription" ? qsTr("Unsubscribe") : qsTr("Remove from Brulion")
                onClicked: {
                    panel.removing = source.sourceId
                    panel.removingName = AppController.routeText(source.modelData, "name")
                    removeCalendar.open()
                }
            }
        }
        // CalDAV: what Brulion does with the calendar.
        Flow {
            Layout.fillWidth: true
            Layout.leftMargin: swatch.width + Theme.space2
            visible: source.kind === "caldav"
            spacing: Theme.space3
            BCheckBox {
                visible: AppController.routeFlag(source.modelData, "holdsEvents")
                text: qsTr("Events in the calendar")
                checked: AppController.routeFlag(source.modelData, "events")
                onToggled: panel.calendars.updateCalendar(source.sourceId, { showEvents: checked })
            }
            BCheckBox {
                visible: AppController.routeFlag(source.modelData, "holdsTasks")
                text: qsTr("Tasks in the notes")
                checked: AppController.routeFlag(source.modelData, "tasks")
                onToggled: panel.calendars.updateCalendar(source.sourceId, { syncTasks: checked })
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: swatch.width + Theme.space2
            visible: source.kind === "caldav" && AppController.routeFlag(source.modelData, "tasks")
            spacing: Theme.space2
            BText { text: qsTr("Page"); variant: "small"; color: Theme.fgMuted }
            BTextField {
                Layout.fillWidth: true
                size: "small"
                text: AppController.routeText(source.modelData, "taskPage")
                leadingIcon: "file-text"
                accessibleName: qsTr("The page the calendar's new tasks go to")
                onEditingFinished: {
                    const page = "" + Js.trim(text)
                    const known = "" + AppController.routeText(source.modelData, "taskPage")
                    if (page !== "" && page !== known)
                        panel.calendars.updateCalendar(source.sourceId, { taskPage: page })
                }
            }
            BButton {
                text: qsTr("Open")
                size: "small"
                variant: "ghost"
                onClicked: AppController.openPage(AppController.routeText(source.modelData, "taskPage"), 0)
            }
        }
        // Subscriptions: how often.
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: swatch.width + Theme.space2
            visible: source.kind === "subscription"
            spacing: Theme.space2
            BText { text: qsTr("Fetch again"); variant: "small"; color: Theme.fgMuted }
            BSegmented {
                size: "small"
                model: [{ value: 60, text: qsTr("hourly") }, { value: 360, text: qsTr("every 6 h") },
                        { value: 1440, text: qsTr("daily") }]
                currentValue: AppController.routeInt(source.modelData, "refreshMinutes", 360)
                accessibleName: qsTr("How often the subscription is fetched")
                onActivated: (value) => panel.calendars.updateSubscription(source.sourceId, { refreshMinutes: value })
            }
        }
    }

    // ---- Status ----------------------------------------------------------------------------------------
    IoCard {
        iconName: panel.calState === "error" ? "circle-alert" : panel.calState === "offline" ? "cloud-off" : "calendar-range"
        iconColor: panel.calState === "error" ? Theme.dangerText : panel.calState === "offline" ? Theme.warningText : Theme.fgMuted
        tone: panel.calState === "error" ? "danger" : "default"
        title: qsTr("Calendars of other programs")
        subtitle: panel.calendars !== null ? panel.calendars.statusText : ""
        trailing: [
            BButton {
                objectName: "calendarsRefreshAll"
                visible: panel.calendars !== null && panel.calendars.hasCalendars
                text: qsTr("Refresh all")
                iconName: "refresh-cw"
                size: "small"
                busy: panel.calendars !== null && panel.calendars.running
                onClicked: panel.calendars.refresh("")
            }
        ]
        Hint {
            text: qsTr("A subscription shows another calendar's events, read-only. A CalDAV account (Nextcloud, Fastmail, iCloud, Radicale, …) shows its events — new ones and changes go back to the server — and keeps its tasks in sync with task lines of this notebook. Everything is kept on this device: other devices set up their own.")
        }
        Hint {
            visible: panel.calendars !== null && panel.calendars.browser
            text: qsTr("In the browser, a calendar's server must allow this website to read it (CORS); many do not. The apps for Linux, Windows and Android have no such limit.")
        }
        BCard {
            Layout.fillWidth: true
            visible: panel.calendars !== null && panel.calendars.needsConfirmation
            tone: "warning"
            ColumnLayout {
                anchors.fill: parent
                spacing: Theme.space2
                BText {
                    Layout.fillWidth: true
                    text: qsTr("Many synced tasks would be deleted at once. Nothing was deleted yet:")
                    wrapMode: Text.Wrap
                }
                BText {
                    Layout.fillWidth: true
                    text: panel.calendars !== null ? Js.joinNonEmpty(panel.calendars.pendingDeletions, ", ") : ""
                    variant: "small"
                    color: Theme.fgMuted
                    wrapMode: Text.Wrap
                    maximumLineCount: 6
                    elide: Text.ElideRight
                }
                BButton {
                    text: qsTr("Delete them")
                    iconName: "trash-2"
                    variant: "danger"
                    size: "small"
                    onClicked: panel.calendars.confirmDeletions()
                }
            }
        }
    }

    // ---- Subscriptions -----------------------------------------------------------------------------------
    IoCard {
        iconName: "rss"
        title: qsTr("Subscriptions")
        subtitle: qsTr("Calendars by their address (ICS, webcal://), read-only")
        Repeater {
            model: panel.subscriptionRows
            delegate: SourceRow {}
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space2
            BTextField {
                id: subscribeField
                objectName: "calendarSubscribeUrl"
                Layout.fillWidth: true
                placeholderText: qsTr("https://… or webcal://…")
                leadingIcon: "link"
                inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase
                onTextEdited: panel.addError = ""
                onAccepted: subscribeButton.clicked()
            }
            BButton {
                id: subscribeButton
                objectName: "calendarSubscribe"
                text: qsTr("Subscribe")
                iconName: "plus"
                enabled: Js.trim(subscribeField.typedText) !== ""
                onClicked: {
                    panel.addError = ""
                    if (panel.calendars.addSubscription(Js.trim(subscribeField.text), "", "") !== "")
                        subscribeField.text = ""
                }
            }
        }
        BText {
            Layout.fillWidth: true
            visible: panel.addError !== ""
            text: panel.addError
            variant: "small"
            color: Theme.dangerText
            wrapMode: Text.Wrap
        }
        Hint {
            visible: panel.subscriptionRows.length === 0
            text: qsTr("For example holidays, a school's or a team's calendar, or Google Calendar's secret address in iCal format.")
        }
    }

    // ---- CalDAV accounts --------------------------------------------------------------------------------
    Repeater {
        model: panel.accountRows
        delegate: IoCard {
            id: account
            required property var modelData
            readonly property string accountId: AppController.routeText(modelData, "id")
            readonly property list<var> rows: AppController.routeList(modelData, "calendars")
            Layout.fillWidth: true
            iconName: "server"
            title: AppController.routeText(modelData, "name")
            subtitle: AppController.routeText(modelData, "server")
            trailing: [
                BIconButton {
                    iconName: "trash-2"
                    label: qsTr("Remove the account")
                    onClicked: {
                        panel.removing = account.accountId
                        panel.removingName = AppController.routeText(account.modelData, "name")
                        removeAccount.open()
                    }
                }
            ]
            Repeater {
                model: account.rows
                delegate: SourceRow {}
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.space2
                spacing: Theme.space2
                BText { text: qsTr("Changed on both sides"); variant: "small"; color: Theme.fgMuted }
                BSegmented {
                    size: "small"
                    model: [{ value: "newer", text: qsTr("Keep the newer") }, { value: "both", text: qsTr("Keep both") }]
                    currentValue: AppController.routeText(account.modelData, "conflicts")
                    accessibleName: qsTr("When a task changed both here and on the server")
                    onActivated: (value) => panel.calendars.updateAccount(account.accountId, { conflicts: value })
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.space2
                BText { text: qsTr("Sync"); variant: "small"; color: Theme.fgMuted }
                BSegmented {
                    size: "small"
                    model: [{ value: 5, text: qsTr("5 min") }, { value: 15, text: qsTr("15 min") },
                            { value: 60, text: qsTr("hourly") }]
                    currentValue: AppController.routeInt(account.modelData, "intervalMinutes", 15)
                    accessibleName: qsTr("How often the account syncs")
                    onActivated: (value) => panel.calendars.updateAccount(account.accountId, { intervalMinutes: value })
                }
            }
            Hint {
                visible: !AppController.routeFlag(account.modelData, "hasPassword")
                color: Theme.dangerText
                text: qsTr("The password of this account is missing on this device: add the account again.")
            }
        }
    }
    BButton {
        objectName: "calendarAddAccount"
        text: qsTr("Add a CalDAV account…")
        iconName: "plus"
        enabled: panel.calendars !== null && panel.calendars.available
        onClicked: accountDialog.openNew()
    }

    // ---- What happened -------------------------------------------------------------------------------
    IoCard {
        visible: panel.logRows.length > 0
        iconName: "history"
        title: qsTr("What happened")
        trailing: [
            BButton {
                text: qsTr("Clear")
                size: "small"
                variant: "ghost"
                onClicked: panel.calendars.clearLog()
            }
        ]
        Repeater {
            model: panel.showAllLog ? panel.logRows : panel.logRows.slice(0, 8)
            delegate: RowLayout {
                id: logRow
                required property var modelData
                readonly property string tone: AppController.routeText(modelData, "tone")
                Layout.fillWidth: true
                spacing: Theme.space2
                BIcon {
                    Layout.alignment: Qt.AlignTop
                    name: logRow.tone === "danger" ? "circle-alert" : logRow.tone === "warning" ? "git-merge" : "check"
                    size: Theme.iconSizeSm
                    color: logRow.tone === "danger" ? Theme.dangerText : logRow.tone === "warning" ? Theme.warningText : Theme.successText
                }
                BText {
                    Layout.fillWidth: true
                    text: panel.joinDots([AppController.routeText(logRow.modelData, "whenText"),
                                          AppController.routeText(logRow.modelData, "name"),
                                          AppController.routeText(logRow.modelData, "text")])
                    variant: "small"
                    wrapMode: Text.Wrap
                }
            }
        }
        BButton {
            visible: panel.logRows.length > 8
            text: panel.showAllLog ? qsTr("Show less") : qsTr("Show all (%1)").arg(panel.logRows.length)
            size: "small"
            variant: "ghost"
            onClicked: panel.showAllLog = !panel.showAllLog
        }
    }

    CalendarAccountDialog { id: accountDialog }

    BMenu {
        id: colorMenu
        Repeater {
            model: panel.calendars !== null ? panel.calendars.colors : panel.noColors
            delegate: BMenuItem {
                required property string modelData
                text: modelData
                swatch: modelData
                onTriggered: panel.calendars.updateCalendar(panel.colorFor, { color: modelData })
            }
        }
    }
    BConfirmDialog {
        id: removeCalendar
        title: qsTr("Remove the calendar from Brulion?")
        subject: panel.removingName
        subjectIcon: "calendar-range"
        message: qsTr("Its events are no longer shown and its tasks no longer synced. The calendar stays on its server; task lines already in the notes stay as they are.")
        confirmText: qsTr("Remove")
        destructive: true
        onAccepted: panel.calendars.removeCalendar(panel.removing)
    }
    BConfirmDialog {
        id: removeAccount
        title: qsTr("Remove the account from Brulion?")
        subject: panel.removingName
        subjectIcon: "server"
        message: qsTr("Its calendars are no longer shown or synced on this device and its password is forgotten. Nothing is deleted on the server or in the notes.")
        confirmText: qsTr("Remove")
        destructive: true
        onAccepted: panel.calendars.removeAccount(panel.removing)
    }
}
