pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// SettingsView — route "settings" ({section?}). Sections: Ogólne (language, theme, text size,
// motion), Edytor (font, size, width, source mode), Notatnik (the vault's shared settings in
// .brulion/config.json: the note modes of new pages and of the journal, journal folder and file
// names, attachments, templates, default reminder time, first day of the week), Przypomnienia (on/off, permissions on Android and in the
// browser, the sounds of reminders and alarms — qml/notify/ReminderSoundSetting —, snooze, alarm
// length, test notification), Synchronizacja (qml/io/SyncSettings: status,
// the sync center, the Nextcloud account), Aplikacja (close to tray, the event log and its
// detailed mode, version, licence, source code; on the desktop "Dla programistów": the switch
// that lets AI agents drive the running app, AppController.remoteControl).
//
// Wide windows: section list on the left, one section on the right. Narrow: all sections in
// one scrolling column.
Item {
    id: root

    property var route: ({})
    readonly property string title: qsTr("Settings")
    property string section: route.section || "general"
    onSectionChanged: scrollToSection()

    // Narrow windows show every section in one list: the route's section is brought to the top,
    // also while the sections above it are still being laid out (until the user scrolls).
    property bool _scrollPending: false
    function scrollToSection() {
        _scrollPending = Theme.narrow
        if (_scrollPending)
            scrollSettle.restart()
        for (let i = 0; i < narrowSections.count; ++i) {
            const item = narrowSections.itemAt(i)
            if (item && root.sections[i].id === root.section)
                scrollTo(i === 0 ? 0 : item.y)
        }
    }
    function scrollTo(y) {
        if (_scrollPending)
            flick.contentY = Math.max(0, Math.min(y, flick.contentHeight - flick.height))
    }
    Timer {
        id: scrollSettle
        interval: 1000 // the sections are laid out by then; later changes are the user's
        onTriggered: root._scrollPending = false
    }

    readonly property var sections: [
        { id: "general", title: qsTr("General"), icon: "settings" },
        { id: "editor", title: qsTr("Editor"), icon: "pencil" },
        { id: "vault", title: qsTr("Notebook"), icon: "notebook-pen" },
        { id: "reminders", title: qsTr("Reminders"), icon: "bell" },
        { id: "sync", title: qsTr("Sync"), icon: "folder-sync" },
        { id: "app", title: qsTr("Application"), icon: "info" }
    ]

    function sectionComponent(id) {
        switch (id) {
        case "editor": return editorSection
        case "vault": return vaultSection
        case "reminders": return remindersSection
        case "sync": return syncSection
        case "app": return appSection
        default: return generalSection
        }
    }

    // ---- Building blocks --------------------------------------------------------------------

    /// A titled group of setting rows.
    component SettingsGroup: ColumnLayout {
        id: group
        property string heading
        property string note
        default property alias rows: cardColumn.data
        Layout.fillWidth: true
        spacing: Theme.space2

        BText {
            Layout.fillWidth: true
            visible: group.heading !== ""
            text: group.heading
            variant: "heading"
            wrapMode: Text.Wrap
            Accessible.role: Accessible.Heading
        }
        BText {
            Layout.fillWidth: true
            visible: group.note !== ""
            text: group.note
            variant: "muted"
            wrapMode: Text.Wrap
        }
        BCard {
            Layout.fillWidth: true
            Layout.topMargin: Theme.space1
            padding: Theme.compact ? Theme.space4 : Theme.space5
            ColumnLayout {
                id: cardColumn
                anchors.fill: parent
                spacing: Theme.space5
            }
        }
    }

    /// A text field that saves one key of the vault config when editing finishes. Its text stays
    /// bound to the config: a change made on another device (sync) shows.
    component ConfigField: BTextField {
        id: field
        required property string key
        property string invalidText: qsTr("This value cannot be used.")
        readonly property string configText: Workspace.config[key] !== undefined ? String(Workspace.config[key]) : ""
        Layout.preferredWidth: Theme.compact ? -1 : 260
        Layout.fillWidth: Theme.compact
        text: configText
        inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
        function save() {
            if (text === configText)
                return
            const change = {}
            change[key] = text.trim()
            errorText = Workspace.setConfig(change) ? "" : invalidText
        }
        /// Save a value picked elsewhere (a clock dialog) at once. The text is not assigned —
        /// that would end its binding to the config, and a value changed later elsewhere would
        /// no longer show (and be written back on the next edit): the binding shows the value.
        function applyValue(value) {
            const change = {}
            change[key] = value
            errorText = Workspace.setConfig(change) ? "" : invalidText
            // Typed text the pick replaces, when the config already had that value.
            if (text !== configText)
                text = Qt.binding(() => field.configText)
        }
        onEditingFinished: save()
    }

    /// Granted / not granted, with a button to ask.
    component PermissionRow: ShellSettingRow {
        id: permission
        required property string name
        readonly property bool granted: AppController.permissions[name] === true
        BBadge {
            Layout.alignment: Qt.AlignVCenter
            text: permission.granted ? qsTr("Allowed") : qsTr("Not allowed")
            variant: permission.granted ? "success" : "warning"
        }
        BButton {
            visible: !permission.granted
            size: "small"
            text: qsTr("Allow")
            onClicked: AppController.requestPermission(permission.name)
        }
    }

    // ---- Layout -----------------------------------------------------------------------------

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // Section list (wide windows).
        Rectangle {
            visible: !Theme.narrow
            Layout.fillHeight: true
            Layout.preferredWidth: 232
            color: Theme.bg

            Column {
                anchors.fill: parent
                anchors.margins: Theme.space3
                anchors.topMargin: Theme.space5
                spacing: 2

                Repeater {
                    model: root.sections
                    delegate: BListItem {
                        required property var modelData
                        width: parent.width
                        text: modelData.title
                        iconName: modelData.icon
                        selected: root.section === modelData.id
                        selectionBar: true
                        onClicked: AppController.replaceRoute({ view: "settings", section: modelData.id })
                    }
                }
            }
            Rectangle {
                anchors.right: parent.right
                width: 1
                height: parent.height
                color: Theme.borderSubtle
            }
        }

        Flickable {
            id: flick
            TouchFlick.enabled: true
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: width
            // Narrow: room below the last row for the floating capture button (56 + margin).
            contentHeight: content.implicitHeight + 2 * margin + (Theme.narrow ? 56 + Theme.space4 : 0)
            boundsBehavior: Flickable.StopAtBounds
            T.ScrollBar.vertical: BScrollBar {}

            readonly property int margin: Theme.compact ? Theme.space4 : Theme.space8
            onMovementStarted: root._scrollPending = false
            onContentHeightChanged: if (root._scrollPending) root.scrollToSection()

            ColumnLayout {
                id: content
                x: flick.margin
                y: flick.margin
                width: Math.min(flick.width - 2 * flick.margin, 760)
                spacing: Theme.space8

                // Wide: the chosen section.
                Loader {
                    Layout.fillWidth: true
                    visible: !Theme.narrow
                    active: !Theme.narrow
                    sourceComponent: root.sectionComponent(root.section)
                    onLoaded: flick.contentY = 0
                }
                // Narrow: everything, scrolled to the route's section.
                Repeater {
                    id: narrowSections
                    model: Theme.narrow ? root.sections : []
                    delegate: Loader {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        sourceComponent: root.sectionComponent(modelData.id)
                        onLoaded: if (modelData.id === root.section) root.scrollToSection()
                        onYChanged: if (modelData.id === root.section) root.scrollTo(index === 0 ? 0 : y)
                    }
                }
            }
        }
    }

    // ---- Sections -----------------------------------------------------------------------------

    Component {
        id: generalSection
        SettingsGroup {
            heading: qsTr("General")

            ShellSettingRow {
                title: qsTr("Language")
                description: qsTr("The language of menus and messages.")
                BSegmented {
                    accessibleName: qsTr("Language")
                    model: [{ value: "pl", text: "Polski" }, { value: "en", text: "English" }]
                    currentValue: AppSettings.language
                    onActivated: (value) => AppSettings.language = value
                }
            }
            ShellSettingRow {
                title: qsTr("Theme")
                description: qsTr("Light like paper, dark for the evening, or following the system.")
                BSegmented {
                    accessibleName: qsTr("Theme")
                    model: [
                        { value: "system", text: qsTr("System"), icon: "monitor" },
                        { value: "light", text: qsTr("Light"), icon: "sun" },
                        { value: "dark", text: qsTr("Dark"), icon: "moon" }
                    ]
                    currentValue: AppSettings.themeMode
                    onActivated: (value) => AppSettings.themeMode = value
                }
            }
            ShellSettingRow {
                title: qsTr("Text size")
                description: qsTr("Size of all text in the app.")
                BSegmented {
                    accessibleName: qsTr("Text size")
                    model: [
                        { value: 0.9, text: qsTr("Small") },
                        { value: 1.0, text: qsTr("Normal") },
                        { value: 1.15, text: qsTr("Large") },
                        { value: 1.3, text: qsTr("Larger") }
                    ]
                    currentValue: [0.9, 1.0, 1.15, 1.3].reduce((best, v) =>
                        Math.abs(v - AppSettings.fontScale) < Math.abs(best - AppSettings.fontScale) ? v : best, 1.0)
                    onActivated: (value) => AppSettings.fontScale = value
                }
            }
            BSwitch {
                Layout.fillWidth: true
                text: qsTr("Reduce motion")
                description: qsTr("Turn off animations.")
                checked: AppSettings.reducedMotion
                onToggled: AppSettings.reducedMotion = checked
            }
        }
    }

    Component {
        id: editorSection
        SettingsGroup {
            heading: qsTr("Editor")

            ShellSettingRow {
                title: qsTr("Font")
                description: qsTr("Font of your notes; the rest of the app uses the system font.")
                BSegmented {
                    accessibleName: qsTr("Font")
                    model: [{ value: "serif", text: qsTr("Serif") }, { value: "sans", text: qsTr("Sans-serif") }]
                    currentValue: AppSettings.editorFont
                    onActivated: (value) => AppSettings.editorFont = value
                }
            }
            ShellSettingRow {
                title: qsTr("Font size")
                BSegmented {
                    accessibleName: qsTr("Font size")
                    model: [14, 16, 18, 20, 22].map(v => ({ value: v, text: String(v) }))
                    currentValue: AppSettings.editorFontSize
                    onActivated: (value) => AppSettings.editorFontSize = value
                }
            }
            ShellSettingRow {
                title: qsTr("Text width")
                description: qsTr("Long lines are hard to read; the editor keeps text within this width.")
                BSegmented {
                    accessibleName: qsTr("Text width")
                    model: [
                        { value: 640, text: qsTr("Narrow") },
                        { value: 760, text: qsTr("Normal", "text width") },
                        { value: 960, text: qsTr("Wide") },
                        { value: 0, text: qsTr("Full") }
                    ]
                    currentValue: AppSettings.editorWidth
                    onActivated: (value) => AppSettings.editorWidth = value
                }
            }
            BSwitch {
                Layout.fillWidth: true
                text: qsTr("Open pages as Markdown source")
                description: qsTr("Show the whole file as text instead of blocks. Ctrl+E switches for one page.")
                checked: AppSettings.editorSourceMode
                onToggled: AppSettings.editorSourceMode = checked
            }
            BSwitch {
                objectName: "settingsWebPictures"
                Layout.fillWidth: true
                text: qsTr("Show pictures from the internet without asking")
                description: qsTr("Off: a picture with a web address waits for “Show”, so opening a synced or imported note tells no site that you read it. Pictures saved to the notebook are always shown.")
                checked: AppSettings.webPicturesShown
                onToggled: AppSettings.webPicturesShown = checked
            }
            // Preview.
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: preview.implicitHeight + 2 * Theme.space4
                radius: Theme.radius
                color: Theme.bgSunken
                BText {
                    id: preview
                    anchors.fill: parent
                    anchors.margins: Theme.space4
                    text: qsTr("Zażółć gęślą jaźń — a sample of your notes in this font and size.")
                    font.family: Theme.fontEditor
                    font.pixelSize: Theme.textEditor // exactly as in the editor
                    lineHeight: Theme.lineHeightPx(font.pixelSize, Theme.lineHeightLoose)
                    wrapMode: Text.Wrap
                }
            }
        }
    }

    Component {
        id: vaultSection
        SettingsGroup {
            heading: qsTr("Notebook")
            note: qsTr("These settings are saved in the notebook (.brulion/config.json) and shared by all your devices.")

            ShellSettingRow {
                title: Workspace.name
                description: Workspace.rootPath
                BButton {
                    text: qsTr("Open another…")
                    iconName: "folder-open"
                    size: "small"
                    onClicked: AppController.closeVault()
                }
            }
            ShellSettingRow {
                title: qsTr("Default mode of new pages")
                description: qsTr("For pages that do not choose a mode themselves. The new-page dialog starts with it.")
                BSegmented {
                    objectName: "pageModeSetting"
                    accessibleName: qsTr("Default mode of new pages")
                    model: [
                        { value: "document", text: qsTr("Document"), icon: "file-text" },
                        { value: "outline", text: qsTr("Outline"), icon: "list" }
                    ]
                    currentValue: Workspace.config.pageMode
                    onActivated: (value) => Workspace.setConfig({ pageMode: value })
                }
            }
            ShellSettingRow {
                title: qsTr("Journal mode")
                description: qsTr("Daily notes as an outline (like Logseq) or as documents. A day switched on its own keeps its mode.")
                BSegmented {
                    objectName: "journalModeSetting"
                    accessibleName: qsTr("Journal mode")
                    // The same order as the row above (and the new-page dialog): the two choices
                    // line up, whichever is selected.
                    model: [
                        { value: "document", text: qsTr("Document"), icon: "file-text" },
                        { value: "outline", text: qsTr("Outline"), icon: "list" }
                    ]
                    currentValue: Workspace.config.journalMode
                    onActivated: (value) => Workspace.setConfig({ journalMode: value })
                }
            }
            ShellSettingRow {
                title: qsTr("Journal folder")
                description: qsTr("Where the daily pages are kept.")
                ConfigField { key: "journalsFolder"; leadingIcon: "folder" }
            }
            ShellSettingRow {
                title: qsTr("Journal file names")
                description: qsTr("Date format, e.g. yyyy-MM-dd or dd.MM.yyyy. Today: %1")
                             .arg(Workspace.journalPath(AppController.today()))
                ConfigField {
                    key: "journalFileFormat"
                    invalidText: qsTr("The format needs a day, a month and a year.")
                }
            }
            ShellSettingRow {
                title: qsTr("Journal template")
                description: qsTr("New daily pages start with this page.")
                ConfigField { key: "journalTemplate"; leadingIcon: "file-text" }
            }
            ShellSettingRow {
                title: qsTr("Templates folder")
                ConfigField { key: "templatesFolder"; leadingIcon: "folder" }
            }
            ShellSettingRow {
                title: qsTr("Attachments folder")
                description: qsTr("Pasted images and dropped files go here.")
                ConfigField { key: "attachmentsFolder"; leadingIcon: "folder" }
            }
            ShellSettingRow {
                title: qsTr("Folder for new pages")
                description: qsTr("Empty: the top of the notebook.")
                ConfigField { key: "newPagesFolder"; leadingIcon: "folder"; placeholderText: qsTr("(top level)") }
            }
            ShellSettingRow {
                title: qsTr("Default reminder time")
                description: qsTr("For reminders written without a time (⏰ 2026-10-15).")
                ConfigField {
                    id: defaultReminderTimeField
                    objectName: "defaultReminderTimeField" // (the shell's self-test)
                    key: "defaultReminderTime"
                    leadingIcon: "clock"
                    invalidText: qsTr("Use HH:mm, e.g. 09:00.")
                    Layout.preferredWidth: 112
                    inputMethodHints: Qt.ImhPreferNumbers
                }
                BIconButton {
                    objectName: "defaultReminderTimeClock"
                    iconName: "clock"
                    label: qsTr("Pick a time on a clock")
                    variant: "secondary"
                    onClicked: defaultReminderTimeDialog.openFor(Workspace.config.defaultReminderTime
                                                                || "09:00")

                    BTimePickerDialog {
                        id: defaultReminderTimeDialog
                        objectName: "defaultReminderTimeDialog" // (the shell's self-test)
                        onPicked: (value) => defaultReminderTimeField.applyValue(value)
                    }
                }
            }
            ShellSettingRow {
                title: qsTr("First day of the week")
                BSegmented {
                    accessibleName: qsTr("First day of the week")
                    model: [
                        { value: 1, text: qsTr("Monday") },
                        { value: 7, text: qsTr("Sunday") },
                        { value: 6, text: qsTr("Saturday") }
                    ]
                    currentValue: Workspace.config.weekStartsOn
                    onActivated: (value) => Workspace.setConfig({ weekStartsOn: value })
                }
            }
            BCard {
                Layout.fillWidth: true
                visible: warnings.text !== ""
                tone: "warning"
                padding: Theme.space3
                BText {
                    id: warnings
                    width: parent.width
                    text: Workspace.revision >= 0 ? Workspace.configWarnings().join("\n") : ""
                    wrapMode: Text.Wrap
                }
            }
        }
    }

    Component {
        id: remindersSection
        SettingsGroup {
            heading: qsTr("Reminders")
            note: AppController.isWasm
                  ? qsTr("In the browser, reminders appear only while Brulion is open in a tab or as an installed app.")
                  : AppController.isAndroid
                    ? qsTr("Reminders and alarms are scheduled in Android and ring even when Brulion is closed.")
                    : qsTr("Reminders appear as system notifications while Brulion runs (also in the tray).")

            BSwitch {
                Layout.fillWidth: true
                text: qsTr("Reminders and alarms")
                description: qsTr("Notify me about ⏰ reminders and ring ⏰! alarms.")
                iconName: "bell"
                checked: AppSettings.remindersEnabled
                onToggled: AppSettings.remindersEnabled = checked
            }
            PermissionRow {
                visible: AppController.isAndroid || (AppController.isWasm && AppController.permissions.notificationsSupported)
                name: "notifications"
                title: qsTr("Notifications")
                description: qsTr("Needed to show reminders.")
            }
            PermissionRow {
                visible: AppController.permissions.exactAlarmsNeeded === true
                name: "exactAlarms"
                title: qsTr("Exact alarms")
                description: qsTr("Without it Android may deliver reminders a few minutes late.")
            }
            PermissionRow {
                visible: AppController.permissions.fullScreenNeeded === true
                name: "fullScreenAlarms"
                title: qsTr("Alarms over the lock screen")
                description: qsTr("Lets an alarm show its full screen like a clock app.")
            }
            ReminderSoundSetting {
                kind: "reminder"
                title: qsTr("Reminder sound")
            }
            ReminderSoundSetting {
                kind: "alarm"
                title: qsTr("Alarm sound")
            }
            ShellSettingRow {
                visible: ReminderSounds.systemSettings
                title: qsTr("System notification settings")
                description: qsTr("Sound, vibration and Do Not Disturb of Brulion's notifications in Android — when a sound does not play as chosen here.")
                BButton {
                    objectName: "reminderSystemSettings"
                    size: "small"
                    text: qsTr("Reminders")
                    onClicked: ReminderSounds.openSystemSettings("reminder")
                }
                BButton {
                    objectName: "alarmSystemSettings"
                    size: "small"
                    text: qsTr("Alarms")
                    onClicked: ReminderSounds.openSystemSettings("alarm")
                }
            }
            ShellSettingRow {
                title: qsTr("Snooze")
                description: qsTr("How long “Snooze” postpones a reminder.")
                BSegmented {
                    accessibleName: qsTr("Snooze")
                    model: [5, 10, 15, 30].map(v => ({ value: v, text: qsTr("%1 min").arg(v) }))
                    currentValue: AppSettings.snoozeMinutes
                    onActivated: (value) => AppSettings.snoozeMinutes = value
                }
            }
            ShellSettingRow {
                title: qsTr("Alarms ring for")
                description: qsTr("An alarm stops by itself after this time.")
                BSegmented {
                    accessibleName: qsTr("Alarms ring for")
                    model: [1, 5, 10, 15].map(v => ({ value: v, text: qsTr("%1 min").arg(v) }))
                    currentValue: AppSettings.alarmRingMinutes
                    onActivated: (value) => AppSettings.alarmRingMinutes = value
                }
            }
            BSwitch {
                Layout.fillWidth: true
                text: qsTr("Show missed reminders")
                description: qsTr("Reminders that were due while the device was off appear when Brulion starts.")
                checked: AppSettings.notifyMissed
                onToggled: AppSettings.notifyMissed = checked
            }
            ShellSettingRow {
                title: qsTr("Test")
                description: qsTr("Check that notifications get through.")
                BButton {
                    text: qsTr("Send a test notification")
                    iconName: "bell-ring"
                    onClicked: AppController.sendTestNotification()
                }
            }
        }
    }

    Component {
        id: syncSection
        // The sync area's section (qml/io/SyncSettings.qml): status of both ways, the sync
        // center, the Nextcloud account form.
        SyncSettings {}
    }

    Component {
        id: appSection
        ColumnLayout {
            spacing: Theme.space8

            SettingsGroup {
                heading: qsTr("Application")

                BSwitch {
                    Layout.fillWidth: true
                    visible: AppController.isDesktop
                    enabled: AppController.trayAvailable
                    text: qsTr("Keep running in the system tray")
                    description: AppController.trayAvailable
                                 ? qsTr("Closing the window keeps Brulion in the tray, so reminders still ring.")
                                 : qsTr("No system tray was found on this desktop.")
                    iconName: "monitor"
                    checked: AppSettings.closeToTray
                    onToggled: AppSettings.closeToTray = checked
                }
                ShellSettingRow {
                    title: qsTr("Event log")
                    description: qsTr("What the app and the sync did, step by step. It stays on this device; share it when something does not work.")
                    BBadge {
                        visible: Logs.problemCount > 0
                        count: Logs.problemCount
                        variant: "warning"
                        Accessible.name: qsTr("%n warning(s) or error(s)", "", Logs.problemCount)
                    }
                    BButton {
                        objectName: "settingsOpenLog"
                        text: qsTr("Open")
                        iconName: "scroll-text"
                        size: "small"
                        onClicked: AppController.navigate({ view: "logs" })
                    }
                }
                BSwitch {
                    objectName: "settingsDetailedLog"
                    Layout.fillWidth: true
                    text: qsTr("Detailed log")
                    description: qsTr("Also writes every request to the server (never note contents or passwords). Turn it on when syncing gives trouble.")
                    iconName: "scroll-text"
                    checked: Logs.detailed
                    onToggled: Logs.detailed = checked
                }
                ShellSettingRow {
                    title: qsTr("Version")
                    description: qsTr("Brulion %1 · %2").arg(AppController.version).arg(AppController.platform)
                    BButton {
                        text: qsTr("About")
                        iconName: "info"
                        size: "small"
                        variant: "ghost"
                        onClicked: AppController.requestUiAction("about")
                    }
                }
                ShellSettingRow {
                    title: qsTr("Licence")
                    description: qsTr("Free software: GNU Affero General Public License 3.0 or later (AGPL-3.0). Icons: Lucide (ISC). Emoji and symbols: Noto Emoji, Noto Sans Math (OFL 1.1).")
                    BButton {
                        text: qsTr("Read")
                        iconName: "external-link"
                        size: "small"
                        variant: "ghost"
                        onClicked: Workspace.openUrl("https://www.gnu.org/licenses/agpl-3.0.html")
                    }
                }
                ShellSettingRow {
                    title: qsTr("Source code")
                    description: "github.com/janusz-bit/brulion"
                    BButton {
                        text: qsTr("Open")
                        iconName: "external-link"
                        size: "small"
                        variant: "ghost"
                        onClicked: Workspace.openUrl("https://github.com/janusz-bit/brulion")
                    }
                }
            }

            // AI agents and scripts driving the running app (desktop: src/app/shell/remotecontrol.h,
            // docs/CLI.md "Remote control"). The setting is this device's; --remote-control turns it
            // on for one run.
            SettingsGroup {
                objectName: "settingsDeveloper"
                visible: AppController.remoteControl.available === true
                heading: qsTr("For developers")

                BSwitch {
                    objectName: "settingsRemoteControl"
                    Layout.fillWidth: true
                    text: qsTr("Allow agents to control the app (local socket)")
                    description: qsTr("AI agents and scripts of your user account can then see what Brulion shows, click, type and take screenshots (brulion-cli app …). Any program you run can then read your notes too — turn it on only while you need it.")
                    iconName: "terminal"
                    checked: AppController.remoteControl.setting === true
                    onToggled: AppSettings.setValue("devtools/remoteControl", checked)
                }
                BText {
                    objectName: "settingsRemoteControlStatus"
                    Layout.fillWidth: true
                    visible: text !== ""
                    variant: "muted"
                    font.pixelSize: Theme.textSm
                    wrapMode: Text.Wrap
                    text: {
                        const rc = AppController.remoteControl
                        if (rc.error)
                            return qsTr("The socket could not be opened: %1").arg(rc.error)
                        if (!rc.active)
                            return ""
                        return (rc.forced ? qsTr("On for this run (started with --remote-control).") + " " : "")
                                + qsTr("Listening at %1").arg(rc.socket)
                    }
                }
            }
        }
    }
}
