pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// CalendarAccountDialog — "Add a CalDAV account" (the sync center's Calendars tab; docs/UI.md
// "Calendars"): the server (a preset fills in what the service needs: Nextcloud, Fastmail, iCloud
// with an app-specific password, Radicale and Baïkal; Google is explained — it needs a sign-in
// Brulion does not have, its secret ICS address can be subscribed to instead), the user and the
// password; "Find calendars" asks the server (Workspace.calendars.discover()) and lists its
// calendars — each with "Events in the calendar" and "Tasks in the notes" (the page they land on
// beside it); "Add" saves the account (the password in an owner-only file) and syncs at once.
BDialog {
    id: control

    readonly property WorkspaceCalendars calendars: Workspace.calendars
    property list<var> found
    property list<string> eventsOn
    property list<string> tasksOn
    property string problem
    property string preset: "other"
    readonly property bool step2: found.length > 0

    function openNew(): void {
        found = []
        eventsOn = []
        tasksOn = []
        problem = ""
        preset = "other"
        serverField.text = ""
        userField.text = ""
        passwordField.text = ""
        open()
    }
    function usePreset(value: string): void {
        preset = value
        switch (value) {
        case "fastmail": serverField.text = "https://caldav.fastmail.com/"; break
        case "icloud": serverField.text = "https://caldav.icloud.com/"; break
        case "nextcloud": if (serverField.text === "") serverField.text = "https://"; break
        }
    }
    function find(): void {
        problem = ""
        const server = "" + Js.trim(serverField.text)
        const user = "" + Js.trim(userField.text)
        const password = passwordField.text
        control.calendars.discover(server, user, password)
    }
    function toggle(which: string, url: string, on: bool): void {
        if (which === "events")
            eventsOn = on ? Js.appendedString(Js.withoutString(eventsOn, url), url) : Js.withoutString(eventsOn, url)
        else
            tasksOn = on ? Js.appendedString(Js.withoutString(tasksOn, url), url) : Js.withoutString(tasksOn, url)
    }

    Connections {
        target: control.calendars
        function onDiscovered(ok: bool, message: string, list: list<var>): void {
            if (!control.visible)
                return
            if (!ok) {
                control.problem = message
                return
            }
            control.found = list
            // Events where a calendar has them; tasks of calendars that hold only tasks.
            control.eventsOn = control.calendars.defaultChoice(control.found, false)
            control.tasksOn = control.calendars.defaultChoice(control.found, true)
        }
    }

    title: qsTr("Add a CalDAV account")
    subtitle: step2 ? qsTr("Choose what Brulion shows and syncs") : qsTr("Nextcloud, Fastmail, iCloud, Radicale, Baïkal and other servers")
    iconName: "server"
    preferredWidth: Theme.dialogWidth + 40
    acceptText: step2 ? qsTr("Add") : qsTr("Find calendars")
    acceptEnabled: step2 ? eventsOn.length + tasksOn.length > 0
                         : Js.trim(serverField.typedText) !== "" && Js.trim(userField.typedText) !== ""
                           && passwordField.typedText !== "" && preset !== "google"
    busy: calendars !== null && calendars.discovering
    initialFocusItem: serverField
    // Finding calendars keeps the dialog open (busy while the server is asked): the second step follows.
    onAcceptRequested: {
        if (!step2)
            find()
    }
    onAccepted: {
        if (!step2)
            return
        const server = "" + Js.trim(serverField.text)
        const user = "" + Js.trim(userField.text)
        const password = passwordField.text
        control.calendars.addAccount(server, user, password, control.eventsOn, control.tasksOn)
    }

    // ---- Step 1: the account -------------------------------------------------------------------------
    ColumnLayout {
        Layout.fillWidth: true
        visible: !control.step2
        spacing: Theme.space3

        BSegmented {
            Layout.fillWidth: true
            fill: true
            size: "small"
            model: [{ value: "nextcloud", text: "Nextcloud" }, { value: "fastmail", text: "Fastmail" },
                    { value: "icloud", text: "iCloud" }, { value: "google", text: "Google" },
                    { value: "other", text: qsTr("Other") }]
            currentValue: control.preset
            accessibleName: qsTr("Service")
            onActivated: (value) => control.usePreset("" + value)
        }
        BText {
            Layout.fillWidth: true
            visible: text !== ""
            variant: "small"
            color: control.preset === "google" ? Theme.warningText : Theme.fgMuted
            wrapMode: Text.Wrap
            text: {
                switch (control.preset) {
                case "nextcloud": return qsTr("The address of your Nextcloud, and an app password (Settings → Security → Devices & sessions).")
                case "fastmail": return qsTr("Use an app password made for CalDAV (Settings → Privacy & Security → App passwords).")
                case "icloud": return qsTr("Your Apple ID and an app-specific password (account.apple.com → Sign-In and Security → App-Specific Passwords).")
                case "google": return qsTr("Google's CalDAV needs a sign-in through Google (OAuth), which Brulion does not have. Subscribe to the calendar's secret address in iCal format instead (Google Calendar → Settings → the calendar → Integrate calendar): its events show in the calendar, read-only.")
                }
                return qsTr("The address of the server (Radicale, Baïkal, SOGo, …) or of a calendar on it. Brulion finds the calendars by itself.")
            }
        }
        BTextField {
            id: serverField
            objectName: "caldavServer"
            Layout.fillWidth: true
            visible: control.preset !== "google"
            placeholderText: qsTr("Server address (https://…)")
            leadingIcon: "server"
            inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase
            EnterKey.type: Qt.EnterKeyNext
        }
        BTextField {
            id: userField
            objectName: "caldavUser"
            Layout.fillWidth: true
            visible: control.preset !== "google"
            placeholderText: control.preset === "icloud" ? qsTr("Apple ID (e-mail)") : qsTr("User name")
            leadingIcon: "at-sign"
            inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
            EnterKey.type: Qt.EnterKeyNext
        }
        BTextField {
            id: passwordField
            objectName: "caldavPassword"
            Layout.fillWidth: true
            visible: control.preset !== "google"
            placeholderText: qsTr("App password")
            leadingIcon: "lock"
            echoMode: TextInput.Password
            inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
            onAccepted: control.tryAccept()
        }
        BText {
            Layout.fillWidth: true
            visible: control.problem !== ""
            text: control.problem
            variant: "small"
            color: Theme.dangerText
            wrapMode: Text.Wrap
        }
        BText {
            Layout.fillWidth: true
            visible: control.calendars !== null && control.calendars.browser
            text: qsTr("In the browser, the server must allow this website to read it (CORS); many servers do not. The apps for Linux, Windows and Android have no such limit.")
            variant: "small"
            color: Theme.fgMuted
            wrapMode: Text.Wrap
        }
    }

    // ---- Step 2: the calendars ------------------------------------------------------------------------
    ColumnLayout {
        Layout.fillWidth: true
        visible: control.step2
        spacing: Theme.space2

        BText {
            Layout.fillWidth: true
            text: qsTr("Events show in Brulion's calendar and can be added and changed there. Tasks become task lines on a page of the notebook and stay in sync both ways.")
            variant: "small"
            color: Theme.fgMuted
            wrapMode: Text.Wrap
        }
        Repeater {
            model: control.found
            delegate: ColumnLayout {
                id: foundRow
                required property var modelData
                readonly property string url: AppController.routeText(modelData, "url")
                readonly property bool hasEvents: AppController.routeFlag(modelData, "events")
                readonly property bool hasTasks: AppController.routeFlag(modelData, "tasks")
                Layout.fillWidth: true
                spacing: Theme.space1
                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.space2
                    spacing: Theme.space2
                    Rectangle {
                        Layout.alignment: Qt.AlignVCenter
                        width: 12
                        height: 12
                        radius: 6
                        readonly property string colorText: AppController.routeText(foundRow.modelData, "color")
                        color: colorText
                    }
                    BText {
                        Layout.fillWidth: true
                        text: AppController.routeText(foundRow.modelData, "name")
                        font.weight: Theme.weightSemiBold
                        elide: Text.ElideRight
                    }
                    BBadge {
                        visible: AppController.routeFlag(foundRow.modelData, "readOnly")
                        text: qsTr("Read-only")
                    }
                }
                BCheckBox {
                    Layout.fillWidth: true
                    visible: foundRow.hasEvents
                    text: qsTr("Events in the calendar")
                    checked: control.eventsOn.indexOf(foundRow.url) >= 0
                    onToggled: control.toggle("events", foundRow.url, checked)
                }
                BCheckBox {
                    Layout.fillWidth: true
                    visible: foundRow.hasTasks
                    text: qsTr("Tasks in the notes")
                    description: qsTr("On the page “%1”").arg(AppController.routeText(foundRow.modelData, "taskPage"))
                    checked: control.tasksOn.indexOf(foundRow.url) >= 0
                    onToggled: control.toggle("tasks", foundRow.url, checked)
                }
            }
        }
    }
}
